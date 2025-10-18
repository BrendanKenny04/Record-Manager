# Record Manager

## Implementation Details

Building on the previous storage and buffer managers, this step introduces a record manager, allowing for a much simpler interface. A program that takes advantage of this record manager must start off by running
~~~c
initRecordManager(NULL);
~~~
<small>[The pointer you pass into it does not have any effect within this simpler implementation.]</small>

The main functions of interest are:
~~~c
//Initialization Functions:
createSchema (int numAttr, char **attrNames, DataType *dataTypes, int *typeLength, int keySize, int *keys);
createTable (char *name, Schema *schema);
openTable (RM_TableData *rel, char *name);
createRecord (Record **record, Schema *schema);
startScan (RM_TableData *rel, RM_ScanHandle *scan, Expr *cond);
MAKE_STRING_VALUE(result, value);
MAKE_VALUE(result, datatype, value);

//Record Functions:
setAttr (Record *record, Schema *schema, int attrNum, Value *value);
getAttr (Record *record, Schema *schema, int attrNum, Value **value);
insertRecord (RM_TableData *rel, Record *record);
deleteRecord (RM_TableData *rel, RID id);
updateRecord (RM_TableData *rel, Record *record);

//Scans:

next (RM_ScanHandle *scan, Record *record);

~~~

### Initialization Functions

createSchema will create and return a schema with the specified parameters.

createTable will create a table based on an already created schema, but does not provide a reference on where to find that table.

openTable will find the table with the given name and set the input table to refer to it.

createRecord will allocate space for a record and put the pointer to it into the inputted record pointer.

startScan will initialize a scan through the given table under the given conditional expression.

make_value, make_string_value, and the various make_x functions in expr.h are useful for generating 'value' and 'expr' structs to use in future functions.

### Record Functions

setAttr takes in a value struct and sets the given attribute's value to the value held in the 'value' struct.

getAttr works in reverse, taking a record and setting the value of a 'value' struct to the specified attribute's value

insertRecord inserts the given record into the given table

deleteRecord deletes the record at the given RID from the given table

updateRecord updates the record at the given RID with the data in the given record in the given table

### Scan functions

next will return the next record (in the scan's table) that follows the given scan's condition into the given record pointer.

## Test Execution

To successfully run this code, you will need to first clone this repository (or download the ZIP file). There are no dependencies other than a functioning C compiler with a standard library, so everything is contained within the repository.

###

Once you have cloned the repository, the execution itself is quite easy.

The two tests included in this repository can be executed using the included [Makefile](https://www.gnu.org/software/make/make.html) (so long as you are using [gcc](https://gcc.gnu.org/)).
'make test' will create the output files, and 'make check' will execute them.

~~~bash
> make test
gcc test_assign3_1.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_assign3
gcc test_expr.c record_mgr.c expr.c rm_serializer.c buffer_mgr.c buffer_mgr_stat.c storage_mgr.c dberror.c -o test_expr

> make check
./test_assign3
. . .
~~~

<small><small>(make check's full output not included, as it is quite long. check the video recording if you really want to see it)</small></small>


## Video Recording
[Link to the Video of Code Executing](https://www.loom.com/share/8b3bae2705624a09bf8fd94ff02c1368?sid=e3ab68b3-0b76-476f-8190-9801b2c1ca58)

During this video, my code ran so much slower than normal it was almost unbelievable. I don't know how loom can possibly use that many resources but it cannot be healthy.