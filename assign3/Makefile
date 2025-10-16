pull:
	cd ../
	git stash
	git pull
push:
	touch record_mgr
	rm record_mgr
	touch test_assign3
	rm test_assign3
	touch test_expr
	rm test_expr
	touch memy.txt
	rm memy.txt
	cd ../
	git add -A
	git commit -m "small update"
	git push
test:
	gcc test_assign3_1.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_assign3
	./test_assign3
	gcc test_expr.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_expr
	./test_expr