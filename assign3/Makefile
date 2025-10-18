test:
	gcc test_assign3_1.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_assign3
	gcc test_expr.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_expr
	
check:
	./test_assign3
	./test_expr