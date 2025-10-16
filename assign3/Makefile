pull:
	cd ../
	git stash
	git pull
push:
	cd ../
	git add -A
	git commit -m "small update"
	git push

test:
	gcc test_assign3_1.c test_expr.c record_mgr.c exrp.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c
	./a.out