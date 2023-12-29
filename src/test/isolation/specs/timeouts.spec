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
setup		{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step sto	{ SET statement_timeout = '10ms'; }
step lto	{ SET lock_timeout = '10ms'; }
step lsto	{ SET lock_timeout = '10ms'; SET statement_timeout = '10s'; }
step slto	{ SET lock_timeout = '10s'; SET statement_timeout = '10ms'; }
step locktbl	{ LOCK TABLE accounts; }
step update	{ DELETE FROM accounts WHERE accountid = 'checking'; }
teardown	{ ABORT; }

session s3
step s3_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step stto	{ SET statement_timeout = '1ms'; SET transaction_timeout = '1s'; }
step tsto	{ SET statement_timeout = '1s'; SET transaction_timeout = '1ms'; }
step sleep	{ SELECT pg_sleep(0.1); }
step abort	{ ABORT; }

session s4
step s4_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step itto	{ SET idle_in_transaction_session_timeout = '1ms'; SET transaction_timeout = '1s'; }

session s5
step s5_begin	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step tito	{ SET idle_in_transaction_session_timeout = '1s'; SET transaction_timeout = '1ms'; }

session s6
step wait_check	{ SELECT pg_sleep(0.1); }
step s3_check	{ SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/s3'; }
step s4_check	{ SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/s4'; }
step s5_check	{ SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/s5'; }
step s7_check	{ SELECT count(*) FROM pg_stat_activity WHERE application_name = 'isolation/timeouts/s7'; }

session s7
step s7_begin
{
    BEGIN ISOLATION LEVEL READ COMMITTED;
    SET transaction_timeout = '150ms';
}
step s7_commit_and_chain { COMMIT AND CHAIN; }
# to test that quick query does not restart transaction_timeout
step s7_select_1 { SELECT 1; }
step s7_sleep	{ SELECT pg_sleep(0.1); }

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

# statement timeout expires first
permutation stto s3_begin sleep s3_check abort
# transaction timeout expires first, session s3 FATAL-out
permutation tsto s3_begin wait_check s3_check
# idle in transaction timeout expires first, session s4 FATAL-out
permutation itto s4_begin wait_check s4_check
# transaction timeout expires first, session s5 FATAL-out
permutation tito s5_begin wait_check s5_check
# transaction timeout expires in presence of query flow
# session s7 FATAL-out sleeping in last wait_check only
permutation s7_begin s7_sleep s7_commit_and_chain s7_sleep s7_select_1 wait_check s7_check
