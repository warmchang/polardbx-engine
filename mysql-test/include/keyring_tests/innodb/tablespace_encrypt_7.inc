################################################################################
# InnoDB transparent tablespace data encryption for general shared tablespace.
# This test case will test
#    - Test 1
#        - 1.1 : Crash just before flushing page 0 at the end of (un)encryption
#        - 1.2 : Crash just after flushing page 0 at the end of (un)encryption
#    - Test 2
#        - 2.1 : Crash just before updating tablespace flags on page 0
#        - 2.2 : Crash just before resetting progress on page 0
#    - Test 3
#        - A scneario in which tablespace key found during REDO log scan is
#          old whereas tablespace key on disk is new. Here, REDO log scan key
#          should be discarded and new tablespace key should be used instead.
################################################################################

# Waiting time when (re)starting the server
--let $explicit_default_wait_counter=10000

--disable_query_log
call mtr.add_suppression("'Error generating data for Data ID:");
call mtr.add_suppression("Check keyring fail, please check the keyring is loaded.");
call mtr.add_suppression("Encryption can't find master key, please check the keyring is loaded.");
call mtr.add_suppression("Encryption information in datafile: encrypt_ts.ibd can't be decrypted, please confirm that keyring is loaded.");
call mtr.add_suppression("\\[ERROR\\] \\[[^]]*\\] \\[[^]]*\\] Failed to decrypt encryption information, found unexpected version of it!");
call mtr.add_suppression("\\[WARNING\\] \\[[^]]*\\] \\[[^]]*\\] Decrypting a page in doublewrite file failed:");
call mtr.add_suppression("\\[WARNING\\] \\[[^]]*\\] \\[[^]]*\\] Encryption key missing:");
--enable_query_log

--source include/make_consensus_args.inc

--echo #########################################################################
--echo # START : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--echo #########
--echo # SETUP #
--echo #########

# Create an Unencrypted tablespace
CREATE TABLESPACE encrypt_ts ADD DATAFILE 'encrypt_ts.ibd' ENGINE=InnoDB ENCRYPTION="N";
# Create an unencrypted table in tablespace
CREATE TABLE t1(c1 char(100)) ENGINE=InnoDB TABLESPACE encrypt_ts;
# Insert few rows in table
--disable_query_log
INSERT INTO t1 VALUES ("SOME VALUES");
let $counter=12;
while ($counter>0)
{
  INSERT INTO test.t1 SELECT * FROM test.t1;
  dec $counter;
}
--enable_query_log

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

# Try to alter tablespace to be encrypted. Should fail as keyring is not laoded.
--error ER_CANNOT_FIND_KEY_IN_KEYRING
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';

--let $rc=1
--echo #-------------------------- TEST 0 -------------------------------------#

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

--echo ########################################################################
--echo # ALTER TABLESPACE 1 :    Unencrypted => Encrypted                     #
--echo #                         crash just before flushing page 0 at the end #
--echo ########################################################################
--echo # Set process to crash just before flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_after_ddl_entry';

--echo # Encrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/start_mysqld_no_echo.inc
--inc $rc
--echo # Wait for Encryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'Y'
FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Encryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

# We shouldn't be able to read t1 records as it is encrypted now and keyring
# plugin is not loaded.
--error ER_CANNOT_FIND_KEY_IN_KEYRING
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo ########################################################################
--echo # ALTER TABLESPACE 2 :    Encrypted => Unencrypted                     #
--echo #                         crash just before flushing page 0 at the end #
--echo ########################################################################
--echo # Set process to crash just before flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_after_ddl_entry';

--echo # Unencrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='N';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Unencryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'N'
FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Unencryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
# We should be able to read t1 records even without keyring plugin as it should
# be unencrypted now.
SELECT * FROM t1 LIMIT 10;

--echo #-------------------------- TEST 1 -------------------------------------#

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

--echo ########################################################################
--echo # ALTER TABLESPACE 1 :    Unencrypted => Encrypted                     #
--echo #                         crash just before flushing page 0 at the end #
--echo ########################################################################
--echo # Set process to crash just before flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_before_flushing_page_0';

--echo # Encrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Encryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'Y'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Encryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

# We shouldn't be able to read t1 records as it is encrypted now and keyring
# plugin is not loaded.
--error ER_CANNOT_FIND_KEY_IN_KEYRING
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo ########################################################################
--echo # ALTER TABLESPACE 2 :    Encrypted => Unencrypted                     #
--echo #                         crash just before flushing page 0 at the end #
--echo ########################################################################
--echo # Set process to crash just before flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_before_flushing_page_0';

--echo # Unencrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='N';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Unencryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'N'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Unencryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
# We should be able to read t1 records even without keyring plugin as it should
# be unencrypted now.
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

--echo ########################################################################
--echo # ALTER TABLESPACE 3 :    Unencrypted => Encrypted                     #
--echo #                         crash just after flushing page 0 at the end  #
--echo ########################################################################
--echo # Set process to crash just after flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_after_flushing_page_0';

--echo # Encrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Encryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'Y'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Encryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

# We shouldn't be able to read t1 records as it is encrypted now and keyring
# plugin is not loaded.
--error ER_CANNOT_FIND_KEY_IN_KEYRING
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args --performance-schema-consumer-events-stages-current=ON;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo ########################################################################
--echo # ALTER TABLESPACE 4 :    Encrypted => Unencrypted                     #
--echo #                         crash just after flushing page 0 at the end  #
--echo ########################################################################
--echo # Set process to crash just after flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_after_flushing_page_0';

--echo # Unencrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='N';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Unencryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'N'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Unencryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
# We should be able to read t1 records even without keyring plugin as it should
# be unencrypted now.
SELECT * FROM t1 LIMIT 10;

--echo #-------------------------- TEST 2 -------------------------------------#

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo ########################################################################
--echo # ALTER TABLESPACE 5 :  Encrypted => unencrypted                       #
--echo #                       crash just before updating ts flags on page0   #
--echo ########################################################################
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';
--echo # Set process to crash just after flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_before_updating_flags';

--echo # Unencrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='N';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Unencryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'N'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Unencryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
# We should be able to read t1 records even without keyring plugin as it should
# be unencrypted now.
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo ########################################################################
--echo # ALTER TABLESPACE 6 :  Encrypted => unencrypted                       #
--echo #                       crash just before resetting progress onpage 0  #
--echo ########################################################################
ALTER TABLESPACE encrypt_ts ENCRYPTION='Y';
--echo # Set process to crash just after flushing page 0 at the end
SET SESSION debug= '+d,alter_encrypt_tablespace_crash_before_resetting_progress';

--echo # Unencrypt the tablespace. It will cause crash.
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts ENCRYPTION='N';

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN after crash
--echo #########################################################################
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo # Wait for Unencryption processing to finish in background thread
let $wait_condition = SELECT ENCRYPTION = 'N'
        FROM INFORMATION_SCHEMA.INNODB_TABLESPACES
        WHERE NAME='encrypt_ts';
--source include/wait_condition.inc

# Make sure ts file is updated with new records in table
set global innodb_buf_flush_list_now = 1;

--echo # After restart/recovery, check that Unencryption was roll-forward
SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
SELECT * FROM t1 LIMIT 10;

--echo #########################################################################
--echo # RESTART $rc: WITHOUT KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_backup_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args;
--source include/restart_mysqld_no_echo.inc
--inc $rc

SELECT NAME, ENCRYPTION FROM INFORMATION_SCHEMA.INNODB_TABLESPACES WHERE NAME='encrypt_ts';
# We should be able to read t1 records even without keyring plugin as it should
# be unencrypted now.
SELECT * FROM t1 LIMIT 10;

--echo #-------------------------- TEST 3 -------------------------------------#

--echo #########################################################################
--echo # RESTART $rc : WITH KEYRING PLUGIN
--echo #########################################################################
--source include/keyring_tests/helper/instance_restore_manifest.inc
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args --innodb_doublewrite=0;
--source include/restart_mysqld_no_echo.inc
--inc $rc

DROP TABLE t1;
DROP TABLESPACE encrypt_ts;
CREATE TABLESPACE encrypt_ts ADD DATAFILE 'encrypt_ts.ibd';
CREATE TABLE t1 (C CHAR(10)) TABLESPACE=encrypt_ts;
SET GLOBAL innodb_limit_optimistic_insert_debug=2;
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");
INSERT INTO t1 VALUES("SOMEVALUE");

--echo # Make sure checkpoint is not moved
SET GLOBAL innodb_log_checkpoint_now = ON;
SET GLOBAL innodb_page_cleaner_disabled_debug = 1;
SET GLOBAL innodb_dict_stats_disabled_debug = 1;
SET GLOBAL innodb_master_thread_disabled_debug = 1;

--echo # Following encryption will create a new tablespace key (KEY1)
--echo # KEY1 will be written on REDO log
ALTER TABLESPACE encrypt_ts encryption='Y';

--echo # Following unencryption will remove tablespace key
ALTER TABLESPACE encrypt_ts encryption='N';

# Force encryption to crash after page 6 is encrypted
SET SESSION debug= '+d,alter_encrypt_tablespace_page_6';
# Force every page to be flushed after its dirtied
SET SESSION debug= '+d,flush_each_dirtied_page';

--echo # Following encryption will create a new tablespace key (KEY2)
--echo # KEY2 will be written on REDO log
--echo # Flush dirtied pages encrypted with KEY2 before crash
--source include/expect_crash.inc
--error 0,CR_SERVER_LOST,ER_INTERNAL_ERROR
ALTER TABLESPACE encrypt_ts encryption='Y';

--echo #########################################################################
--echo # RESTART $rc: WITH KEYRING PLUGIN after crash
--echo #########################################################################
# Without fix, following restart would crash reporting page corruption
let $restart_parameters = restart: $PLUGIN_DIR_OPT $consensus_args --innodb_doublewrite=0 --debug=d,dont_update_key_found_during_REDO_scan;
--source include/start_mysqld_no_echo.inc
--inc $rc

--echo ###########
--echo # Cleanup #
--echo ###########
DROP TABLE t1;
DROP TABLESPACE encrypt_ts;
