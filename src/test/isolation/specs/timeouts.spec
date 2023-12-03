# Simple tests for statement_timeout and lock_timeout features

setup
{
 CREATE TABLE accounts (accountid text PRIMARY KEY, balance numeric not null);
 INSERT INTO accounts VALUES ('checking', 600), ('savings', 600);
}

teardown
{
 DROP TABLE accounts;
}

session s1
setup		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step rdtbl	{ SELECT * FROM accounts; }
step wrtbl	{ UPDATE accounts SET balance = balance + 100; }
teardown	{ ABORT; }

session s2
setup		{ SET transaction_timeout = '10s'; SET idle_in_transaction_session_timeout = '10s'; BEGIN ISOLATION LEVEL READ COMMITTED; }
step sto	{ SET statement_timeout = '10ms'; }
step lto	{ SET lock_timeout = '10ms'; }
step lsto	{ SET lock_timeout = '10ms'; SET statement_timeout = '10s'; }
step slto	{ SET lock_timeout = '10s'; SET statement_timeout = '10ms'; }
step locktbl	{ LOCK TABLE accounts; }
step update	{ DELETE FROM accounts WHERE accountid = 'checking'; }
teardown	{ ABORT; }

session stt1
# enable statement_timeout to check interaction
setup			{ SET statement_timeout = '10s'; SET lock_timeout = '10s'; }
step stt1_set	{ SET transaction_timeout = '1ms'; }
step stt1_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step sleep_here	{ SELECT pg_sleep(1); }

session stt2
setup			{ SET statement_timeout = '10s'; SET lock_timeout = '10s'; }
step stt2_set	{ SET transaction_timeout = '1ms'; }
step stt2_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
# Session stt2 is terminated in the background. However, isolation tester needs a step to observe it.

session stt3
step sleep_there{ SELECT pg_sleep(0.1); }
# Observe that stt2\itt4 died
step stt3_check_stt2 { SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/stt2' }
step stt3_check_itt4 { SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/itt4' }

session itt4
step itt4_set	{ SET idle_in_transaction_session_timeout = '1ms'; SET statement_timeout = '10s'; SET lock_timeout = '10s'; SET transaction_timeout = '10s'; }
step itt4_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }

# It's possible that the isolation tester will not observe the final
# steps as "waiting", thanks to the relatively short timeouts we use.
# We can ensure consistent test output by marking those steps with (*).

# statement timeout, table-level lock
permutation rdtbl sto locktbl(*)
# lock timeout, table-level lock
permutation rdtbl lto locktbl(*)
# lock timeout expires first, table-level lock
permutation rdtbl lsto locktbl(*)
# statement timeout expires first, table-level lock
permutation rdtbl slto locktbl(*)
# statement timeout, row-level lock
permutation wrtbl sto update(*)
# lock timeout, row-level lock
permutation wrtbl lto update(*)
# lock timeout expires first, row-level lock
permutation wrtbl lsto update(*)
# statement timeout expires first, row-level lock
permutation wrtbl slto update(*)

# timeout of active query, idle transaction timeout
permutation stt1_set stt1_begin sleep_here stt2_set stt2_begin sleep_there stt3_check_stt2 itt4_set itt4_begin sleep_there stt3_check_itt4(*)
# can't run tests after this, sessions stt1, stt2, and itt4 are expected to FATAL-out
