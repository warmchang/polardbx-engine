/* Copyright (c) 2006, 2018, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "sql/log_event.h"
#include "sql/rpl_rli.h"
#include "sql/rpl_rli_ext.h"
#include "bl_consensus_log.h"

bool opt_consensus_index_buf_enabled = false;


void update_consensus_apply_index(Relay_log_info *rli, Log_event *ev) {
  /**
   * 1. xpaxos_replication_channel
   * 2. end_group of current consensus index or not
   */
  if (!rli || !rli->info_thd->xpaxos_replication_channel) return;

  if (rli->is_parallel_exec()) {
    /**
     * if rli->m_consensus_index_buf is not inited
     *  mts will update consensus index in mts_checkpoint_routine
     */
    mts_advance_consensus_apply_index(rli, ev);
  } else {
    if (!ev || ev->future_event_relay_log_pos != ev->consensus_index_end_pos)
      return;

    if (ev->consensus_real_index > consensus_ptr->getAppliedIndex()) {
      consensus_ptr->updateAppliedIndex(ev->consensus_real_index);
      replica_read_manager.update_lsn(ev->consensus_real_index);
    }
  }
}

void mts_init_consensus_apply_index(Relay_log_info *rli,
                                    uint64 consensus_index) {
  /** rli->m_consensus_index_buf will be inited in mts and xpaxos_replication */
  if (!rli || !rli->m_consensus_index_buf) return;

  DBUG_ASSERT(rli->is_parallel_exec());
  DBUG_ASSERT(rli->info_thd->xpaxos_replication_channel);

  rli->m_consensus_index_buf->init_tail(consensus_index);
}

void mts_advance_consensus_apply_index(Relay_log_info *rli, Log_event *ev) {
  /** rli->m_consensus_index_buf will be inited in mts and xpaxos_replication */
  if (!rli || !rli->m_consensus_index_buf) return;

  /** current index group has not finished */
  if (!ev || ev->future_event_relay_log_pos != ev->consensus_index_end_pos)
    return;

  DBUG_ASSERT(rli->is_parallel_exec());
  DBUG_ASSERT(rli->info_thd->xpaxos_replication_channel);

  uint64 consensus_index =
      rli->m_consensus_index_buf->add_index_advance_tail(ev->consensus_real_index);

  if (consensus_index > consensus_ptr->getAppliedIndex()) {
    consensus_ptr->updateAppliedIndex(consensus_index);
    replica_read_manager.update_lsn(consensus_index);
  }
}

void mts_force_consensus_apply_index(Relay_log_info *rli,
                                     uint64 consensus_index) {
  /** rli->m_consensus_index_buf will be inited in mts and xpaxos_replication */
  if (rli && rli->m_consensus_index_buf) {
    DBUG_ASSERT(rli->is_parallel_exec());
    DBUG_ASSERT(rli->info_thd->xpaxos_replication_channel);
    rli->m_consensus_index_buf->force_advance_tail(consensus_index);
  }

  if (consensus_index > consensus_ptr->getAppliedIndex()) {
    consensus_ptr->updateAppliedIndex(consensus_index);
    replica_read_manager.update_lsn(consensus_index);
  }
}

Index_link_buf::Index_link_buf(uint64 capacity)
    : m_capacity(capacity), m_indexes(nullptr), m_tail(0), m_locked(false) {
  if (capacity == 0) return;

  m_indexes = (std::atomic<uint64> *)my_malloc(
      PSI_INSTRUMENT_ME, sizeof(std::atomic<uint64>) * capacity, MYF(0));
  if (m_indexes == nullptr) return;

  for (size_t i = 0; i < capacity; ++i) {
    m_indexes[i].store(0);
  }
}

Index_link_buf::~Index_link_buf() {
  if (m_indexes) my_free(m_indexes);
}

bool Index_link_buf::lock(bool retry) {
  uint retry_times = 0;
  bool expected = false;
  if (m_locked.compare_exchange_strong(expected, true,
                                       std::memory_order_seq_cst))
    return true;

  if (retry) {
    expected = false;
    while (!m_locked.compare_exchange_strong(expected, true,
                                             std::memory_order_seq_cst)) {
      expected = false;
      retry_times++;
      usleep(1000);

      if (retry_times > 10) return false;
    }
    return true;
  }

  return false;
}

void Index_link_buf::unlock() { m_locked.store(false, std::memory_order_seq_cst); }

inline void Index_link_buf::init_tail(uint64 index) {
  m_tail.store(index);
}

inline uint64 Index_link_buf::get_slot_index(uint64 index) const {
  return index % m_capacity;
}

inline uint64 Index_link_buf::add_index_advance_tail(uint64 index) {
  assert(index <= m_tail.load() + m_capacity);
  assert(index > m_tail.load());

  sql_print_information("add index %u", index);

  auto slot_index = get_slot_index(index);
  auto &slot = m_indexes[slot_index];
  slot.store(index, std::memory_order_release);
  return advance_tail();
}

inline uint64 Index_link_buf::advance_tail() {
  if (!lock()) return 0;

  while (true) {
    auto advance_index = m_tail.load() + 1;
    auto slot_index = get_slot_index(advance_index);
    auto &slot = m_indexes[slot_index];

    if (slot.load(std::memory_order_acquire) != advance_index) break;

    m_tail++;
  }

  unlock();
  sql_print_information("advance to tail %u", m_tail.load());
  return m_tail;
}

inline void Index_link_buf::force_advance_tail(uint64 index) {
  if (m_tail.load() >= index) return;

  if (!lock(true)) return;

  if (m_tail.load() >= index) {
    unlock();
    return;
  }

  m_tail.store(index);
  unlock();
}
