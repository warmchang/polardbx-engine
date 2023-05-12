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


#include "sql/trans_proc/returning_parse.h"
#include "sql/auth/auth_acls.h"
#include "sql/protocol.h"
#include "sql/sql_base.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"

namespace im {

/* Constructor */
Lex_returning::Lex_returning(bool is_returning_stmt)
    : m_is_returning_clause(false),
      m_is_returning_call(is_returning_stmt),
      m_with_wild(0),
      m_items() {}

/* Destructor */
Lex_returning::~Lex_returning() {
  DBUG_ENTER("Lex_returning::~Lex_returning");
  DBUG_ASSERT(m_is_returning_clause || m_items.is_empty());

  m_items.empty();
  DBUG_VOID_RETURN;
}

void Lex_returning::reset() {
  DBUG_ENTER("Lex_returning::reset");
  m_is_returning_clause = false;
  m_is_returning_call = false;
  m_with_wild = 0;
  m_items.empty();
  DBUG_VOID_RETURN;
}

Lex_returning &Lex_returning::operator=(const Lex_returning &tmp) {
  m_is_returning_clause = tmp.m_is_returning_clause;
  m_is_returning_call = tmp.m_is_returning_call;
  m_items = tmp.m_items;
  return *this;
}

/* Constructor */
Update_returning_statement::Update_returning_statement(THD *thd)
    : m_thd(thd), m_returning(false), m_lex_returning(nullptr) {
  init();
}

/**
  Init the returning statement context.

  Require it must be come from dbms_trans.returning() call
  and give fields.
*/
void Update_returning_statement::init() {
  Lex_returning *lex_ret;
  DBUG_ENTER("Update_returning_statement::init");
  lex_ret = m_thd->get_lex_returning();

  /* Must be dbms_trans.returning call and item list count > 0 */
  if (lex_ret->is_returning_clause() && lex_ret->is_returning_call()) {
    m_returning = true;
    m_lex_returning = lex_ret;
  }
  DBUG_VOID_RETURN;
}

/**
  Backup the select_lex field_list and with_wild attributes.
*/
class Backup_select_lex_fields {
 public:
  Backup_select_lex_fields(SELECT_LEX *select_lex, List<Item> *fields,
                           uint with_wild)
      : m_select_lex(select_lex),
        m_backup_fields(),
        m_origin_fields(fields),
        m_backup_wild(0) {
    m_backup_fields = select_lex->fields_list;
    m_backup_wild = select_lex->with_wild;
    select_lex->fields_list = *fields;
    select_lex->with_wild = with_wild;
  }

  ~Backup_select_lex_fields() {
    /* Override the original fields list after expanding "*" */
    *m_origin_fields = m_select_lex->fields_list;
    m_select_lex->fields_list = m_backup_fields;
    m_select_lex->with_wild = m_backup_wild;
  }

 private:
  SELECT_LEX *m_select_lex;
  List<Item> m_backup_fields;
  List<Item> *m_origin_fields;
  uint m_backup_wild;
};

/**
  Itemize all the field_items from procedure parameters.

  @param[in]      thd           thread context
  @param[in]      fields        field items from proc
  @param[in]      select_lex    the update/delete statement lex

  @retval         false         success
  @retval         true          failure
*/
bool Update_returning_statement::itemize_fields(THD *thd, List<Item> &fields,
                                                SELECT_LEX *select_lex) {
  Parse_context pc(thd, select_lex);
  Item *item;
  /* Itemize all the field_item */
  List_iterator<Item> it(fields);
  while ((item = it++)) {
    if (item->itemize(&pc, &item)) return true;
  }
  return false;
}

/**
  Expand "*" and setup all fields.

  @param[in]      thd           thread context
  @param[in]      select_lex    the update/delete statement lex

  @retval         false         success
  @retval         true          failure
*/
bool Update_returning_statement::setup(THD *thd, SELECT_LEX *select_lex) {
  List<Item> *fields;
  uint with_wild;
  DBUG_ENTER("Update_returning_statement::setup");
  if (m_returning) {
    fields = m_lex_returning->get_fields();
    with_wild = m_lex_returning->with_wild();

    /* Itemize all the field_item */
    itemize_fields(thd, *fields, select_lex);

    /* Backup select_lex context, expand * fields */
    if (with_wild > 0) {
      Backup_select_lex_fields bs(select_lex, fields, with_wild);
      if (select_lex->setup_wild(thd)) DBUG_RETURN(true);
    }

    /* Setup all fields */
    if (setup_fields(thd, Ref_item_array(), *fields, SELECT_ACL, NULL, false,
                     false))
      DBUG_RETURN(true);

    /* Prepare the result set */
    result.prepare(thd, *fields, thd->lex->unit);
    if (result.send_result_set_metadata(
            thd, *fields, Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
      DBUG_RETURN(true);
  }

  DBUG_RETURN(false);
}

/**
  Send the row data.

  @param[in]      thd           thread context

  @retval         false         success
  @retval         true          failure
*/
bool Update_returning_statement::send_data(THD *thd) {
  if (m_returning) {
    return result.send_data(thd, *(m_lex_returning->get_fields()));
  }
  return false;
}
/**
  Send the EOF.

  @param[in]      thd           thread context
*/
void Update_returning_statement::send_eof(THD *thd) {
  if (m_returning) {
    result.send_eof(thd);
  }
}

/**
  Only allowed certain sql command has returning clause

  Report error if failed.

  @retval     false       success
  @retval     true        failure
*/
bool deny_returning_clause_by_command(THD *thd, LEX *lex) {
  /**
    If it's the returning call and the sub statement is not update or delete,
    report error here.
    Pls update here if support more command.
  */
  if (thd->get_lex_returning()->is_returning_call() &&
      (lex->is_explain() || (lex->sql_command != SQLCOM_UPDATE &&
                             lex->sql_command != SQLCOM_REPLACE &&
                             lex->sql_command != SQLCOM_DELETE &&
                             lex->sql_command != SQLCOM_INSERT))) {
    my_error(ER_NOT_SUPPORT_RETURNING_CLAUSE, MYF(0));
    return true;
  }
  return false;
}

}  // namespace im
