
#include "../global_defines.h"
#ifndef MYSQL8
#define MYSQL_SERVER
#endif
#include "sql/key.h"
#include "sql/field.h"

#include "meta.h"
#include "log.h"

using namespace Polarx;

namespace rpc_executor {


int SearchKey::store_to_buffer(ExecKeyMeta *exec_key,
                               const ExecKeyMap &expr) {
  int ret = HA_EXEC_SUCCESS;
  KEY *key_info = exec_key->key();
  KeyPartMap current_bit = 1;

  /* Max column supported is 4096 */
  assert(expr.parts() <= 4096);
  assert(expr.parts() <= key_info->user_defined_key_parts);

  key_buffer_.reset(new uchar[key_info->key_length]);
  if (!key_buffer_) {
    ret = HA_ERR_OUT_OF_MEM;
    log_exec_error("new key buffer failed, ret: %d, key_length: %u",
                   ret, key_info->key_length);
  } else {

    // search_key -> key_info -> my_table->record[0]
    int prefix_flag = 1;
    for (uint32_t i = 0; i < key_info->user_defined_key_parts; ++i) {
      KEY_PART_INFO *key_part = &key_info->key_part[i];
      Field *field = key_part->field;
      DataRep data;
      if ((ret = expr.get_field(field->field_name, data))) {
        // Some field is not given by user.
        // This is not an error but should stop here because we can only support
        // prefix seek.
        if (HA_ERR_KEY_NOT_FOUND == ret) {
          prefix_flag = 0;
          ret = HA_EXEC_SUCCESS;
        } else {
          log_exec_error("unexpected error occurd when getting field from "
                         "search_key, ret: %d, field: %s",
                         ret, field->field_name);
        }
      } else {
        if (prefix_flag == 0) {
          ret = HA_ERR_KEY_NOT_FOUND;
          break;
        }
        bitmap_set_bit(field->table->write_set, field->field_index);
        switch (data.type_) {
          case DataRep::SIGNED_INT:
            field->set_notnull();
            field->store(data.v_int_, false);
            break;
          case DataRep::UNSIGNED_INT:
            field->set_notnull();
            field->store(data.v_uint_, true);
            break;
          case DataRep::NULL_TYPE:
            if (field->real_maybe_null()) {
              field->set_null();
            } else {
              impossible_where_ = true;
            }
            break;
          case DataRep::DOUBLE:
            field->set_notnull();
            field->store(data.v_double_);
            break;
          case DataRep::FLOAT:
            field->set_notnull();
            field->store(data.v_float_);
            break;
          case DataRep::BOOL:
            field->set_notnull();
            field->store(data.v_bool_);
            break;
          case DataRep::STRING:
            field->set_notnull();
            field->store(data.v_string_->data(),
                         data.size_,
                         field->charset());
            break;
          default:
            ret = HA_ERR_UNSUPPORTED;
        }
        if (ret) {
          break;
        }
        used_length_ += key_part->store_length;
        used_part_map_ |= current_bit;
        current_bit <<= 1;
      }
    }
  }
  if (!ret) {
    // my_table->record[0] -> key_buffer
    key_copy(key_buffer_.get(), key_info->table->record[0], key_info,
             used_length_);
    using_full_key_ = (((KeyPartMap)1 << key_info->actual_key_parts) - 1) ==
                      used_part_map_;
  }

  return ret;
}
} // namespace executor
