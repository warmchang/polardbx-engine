/* Copyright (c) 2018, 2021, Alibaba and/or its affiliates. All rights reserved.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.
   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL/PolarDB-X Engine hereby grant you an
   additional permission to link the program and your derivative works with the
   separately licensed software that they have included with
   MySQL/PolarDB-X Engine.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef CONSENSUS_INFO_INCLUDE
#define CONSENSUS_INFO_INCLUDE

#include "rpl_info.h"
#include "mysql/components/services/psi_rwlock_bits.h"
#include <atomic>
#include <string>

class Consensus_info : public Rpl_info
{
  friend class Rpl_info_factory;
public:
  Consensus_info(
#ifdef HAVE_PSI_INTERFACE
    PSI_mutex_key *param_key_info_run_lock,
    PSI_mutex_key *param_key_info_data_lock,
    PSI_mutex_key *param_key_info_sleep_lock,
    PSI_mutex_key *param_key_info_thd_lock,
    PSI_mutex_key *param_key_info_data_cond,
    PSI_mutex_key *param_key_info_start_cond,
    PSI_mutex_key *param_key_info_stop_cond,
    PSI_mutex_key *param_key_info_sleep_cond
#endif
  );

  uint64 get_recover_status() { return recover_status; }
  uint64 get_vote_for() { return vote_for; }
  uint64 get_current_term() { return current_term; }
  uint64 get_last_leader_term() { return last_leader_term; }
  uint64 get_start_apply_index() { return start_apply_index; }
  uint64 get_cluster_id() { return cluster_id; }
  std::string& get_cluster_info() { return cluster_info; }
  std::string& get_cluster_learner_info() { return cluster_learner_info; }
  uint64 get_cluster_recover_index() { return cluster_recover_index; }

  void set_recover_status(uint64 recover_status_arg) { recover_status = recover_status_arg; }
  void set_vote_for(uint64 vote_for_arg) { vote_for = vote_for_arg; }
  void set_current_term(uint64 current_term_arg) { current_term = current_term_arg; }
  void set_last_leader_term(uint64 last_leader_term_arg) { last_leader_term = last_leader_term_arg; }
  void set_start_apply_index(uint64 start_apply_index_arg) { start_apply_index = start_apply_index_arg; }
  void set_cluster_id(uint64 cluster_id_arg) { cluster_id = cluster_id_arg; }
  void set_cluster_info(const std::string& cluster_info_arg) { cluster_info = cluster_info_arg; }
  void set_cluster_learner_info(const std::string& cluster_learner_info_arg) { cluster_learner_info = cluster_learner_info_arg; }
  void set_cluster_recover_index(uint64 cluster_recover_index_arg) { cluster_recover_index = cluster_recover_index_arg; }

public:
  int consensus_init_info();
  void end_info();
   /* only slave thread do not need force_new_thd */
  int flush_info(bool force, bool force_new_thd);
  bool set_info_search_keys(Rpl_info_handler *to);
  static size_t get_number_info_consensus_fields();

  virtual const char* get_for_channel_str(bool upper_case __attribute__((unused)) ) const
  {
    return NULL;
  }

  static void set_nullable_fields(MY_BITMAP *nullable_fields);

private:
  bool read_info(Rpl_info_handler *from);
  bool write_info(Rpl_info_handler *to);

private:
  static const int CLUSTER_CONF_STR_LENGTH = 5000;
  PSI_rwlock_key   key_LOCK_consensus_info;
  mysql_mutex_t LOCK_consensus_info;       // protect flush 
  uint64 vote_for;                                      // used by consensus algorithm layer
  uint64 current_term;                                // used by consensus algorithm layer 
  uint64 recover_status;                              // used by replay thread to determine startpoint;  0:followrer  1:leader
  std::atomic<uint64> last_leader_term;        // used by determine apply point
  std::atomic<uint64> start_apply_index;       // used by determine apply point, relay working set 0
                                                               // first leader downgrade set right value
  uint64 cluster_id;                                     // used to identify cluster
  std::string cluster_info;                             // used to store consensus nodes info
  std::string cluster_learner_info;                  // used to store learner nodes info
  uint64 cluster_recover_index;                    // used to recover cluster config
  char consensus_config_name[FN_REFLEN];
};
#endif
