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


#include "my_dbug.h"

#include "sql/auth/auth_acls.h"
#include "sql/auth/sql_security_ctx.h"
#include "sql/package/package_parse.h"
#include "sql/package/proc.h"
#include "sql/protocol.h"

namespace im {

/**
  Generate the parse tree root.

  @param[in]    THD           Thread context
  @param[in]    pt_expr_list  Parameters
  @param[in]    proc          Native predefined proc

  @retval       Parse_tree_root
*/
Parse_tree_root *Proc::PT_evoke(THD *thd, PT_item_list *pt_expr_list,
                                const Proc *proc) const {
  return new (thd->mem_root) PT_proc_type(pt_expr_list, proc);
}

/**
  Send the result meta data by columns definition.

  @param[in]    THD           Thread context

  @retval       true          Failure
  @retval       false         Success
*/
bool Proc::send_result_metadata(THD *thd) const {
  List<Item> field_list;
  Item *item;
  DBUG_ENTER("Proc::send_result_metadata");

  DBUG_ASSERT(m_columns.size() > 0 && m_result_type == Result_type::RESULT_SET);

  for (Columns::const_iterator it = m_columns.begin(); it != m_columns.end();
       it++) {

    /* TODO: Support more field type */
    switch ((*it).type) {
      case MYSQL_TYPE_LONGLONG:
        field_list.push_back(
            item = new Item_int(Name_string((*it).name, (*it).name_len),
                                (*it).size, MY_INT64_NUM_DECIMAL_DIGITS));
        item->maybe_null = 1;
        break;
      case MYSQL_TYPE_VARCHAR:
        field_list.push_back(item =
                                 new Item_empty_string((*it).name, (*it).size));

        item->maybe_null = 1;
        break;
      case MYSQL_TYPE_TIMESTAMP:
        field_list.push_back(item = new Item_temporal(
                                 MYSQL_TYPE_TIMESTAMP,
                                 Name_string((*it).name, strlen((*it).name)), 0,
                                 0));
        break;
      default:
        DBUG_ASSERT(0);
    }
  }
  if (thd->send_result_metadata(&field_list,
                                Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(true);

  DBUG_RETURN(false);
}

/**
  Send the ok or error packet defaultly,
  Override it if any result set.
*/
void Sql_cmd_proc::send_result(THD *thd, bool error) {
  DBUG_ENTER("Sql_cmd_proc::send_result");
  if (!error) {
    DBUG_ASSERT(m_proc->get_result_type() == Proc::Result_type::RESULT_OK);
    my_ok(thd);
  } else {
    DBUG_ASSERT(thd->is_error());
  }
  DBUG_VOID_RETURN;
}

/**
  Check access, require SUPER_ACL defaultly.
  Override it if any other requirement.
*/
bool Sql_cmd_proc::check_access(THD *thd) {
  Security_context *sctx = thd->security_context();

  if (m_priv_type == Priv_type::PRIV_NONE_ACL) {
    return false;
  } else if (m_priv_type == Priv_type::PRIV_SUPER_ACL) {
    if (!sctx->check_access(SUPER_ACL)) {
      my_error(ER_SPECIFIC_ACCESS_DENIED_ERROR, MYF(0), "SUPER");
      return true;
    }
    return false;
  }
  return false;
}

/**
  Check the list of parameters.
  Override it if any other requirement.
*/
bool Sql_cmd_proc::check_parameter() {
  std::size_t actual_size = (m_list == nullptr ? 0 : m_list->elements);
  std::size_t define_size = m_proc->get_parameters()->size();

  const Proc::Parameters *current_parameters = nullptr;
  if (m_proc->get_parameters_list().size() == 0) {
    current_parameters = m_proc->get_parameters();
  } else {
    for (auto parameters : m_proc->get_parameters_list()) {
      if (parameters->size() == actual_size) {
        define_size = parameters->size();
        current_parameters = parameters;
        break;
      }
    }
  }

  if (current_parameters == nullptr || actual_size != define_size) {
    my_error(ER_SP_WRONG_NO_OF_ARGS, MYF(0), "PROCEDURE",
             m_proc->qname().c_str(), define_size, actual_size);
    return true;
  }

  if (actual_size > 0) {
    std::size_t i = 0;
    List_iterator_fast<Item> it(*m_list);
    Item *item;
    while ((item = it++)) {
      if (item->data_type() != m_proc->get_parameters()->at(i)) {
        my_error(ER_NATIVE_PROC_PARAMETER_MISMATCH, MYF(0), i + 1,
                 m_proc->qname().c_str());
        return true;
      }
      i++;
    }
  }
  return false;
}

/**
  Prepare the proc before execution.
*/
bool Sql_cmd_proc::prepare(THD *thd) {
  if (check_parameter() || check_access(thd))
    return true;
  set_prepared();
  return false;
}

/**
  Interface of Proc execution body.

  @param[in]    THD           Thread context

  @retval       true          Failure
  @retval       false         Success
*/
bool Sql_cmd_proc::execute(THD *thd) {
  bool error;
  /* Step 1 : prepare */
  if ((error = prepare(thd))) goto end;

  /* Step 2 : execute */
  if ((error = pc_execute(thd))) goto end;

end:
  /* Step 3 : send result */
  send_result(thd, error);
  return error;
}
} /* namespace im */
