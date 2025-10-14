test: storage_mgr.c dberror.c buffer_mgr.c test_assign2_1.c test_assign2_2.c buffer_mgr_stat.c
	gcc test_assign2_1.c buffer_mgr.c storage_mgr.c dberror.c buffer_mgr_stat.c -o ./test1
	gcc test_assign2_2.c buffer_mgr.c storage_mgr.c dberror.c buffer_mgr_stat.c -o ./test2

check: test1 test2
	./test1
	./test2