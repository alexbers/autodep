#include <Python.h>

#define MAXPATHLEN 256
#define MAXFILEBUFFLEN 2048


static PyObject* py_getparentpid(PyObject* self, PyObject* args)
{
  int ok;
  int pid;
  ok=PyArg_ParseTuple(args, "i", &pid);
  if(!ok)
    return Py_BuildValue("i", 0);

  char filename[MAXPATHLEN];
  snprintf(filename,MAXPATHLEN, "/proc/%d/stat",pid);
  FILE *stat_file_handle=fopen(filename,"r");
  if(stat_file_handle==NULL)
    return Py_BuildValue("i", 0);
  
  char filedata[MAXFILEBUFFLEN];
  size_t bytes_readed=fread(filedata,sizeof(char),MAXFILEBUFFLEN,stat_file_handle);
  if(bytes_readed==0 || bytes_readed>=MAXFILEBUFFLEN) {
	fclose(stat_file_handle);
	return Py_BuildValue("i", 0);
  }
  
  filedata[bytes_readed]=0;
  
  char *beg_scan_offset=rindex(filedata,')');
  if(beg_scan_offset==NULL) {
	fclose(stat_file_handle);
	return Py_BuildValue("i", 0);
  }
  
  pid_t parent_pid;
  int tokens_readed=sscanf(beg_scan_offset,") %*c %d",&parent_pid);
  if(tokens_readed!=1) {
	fclose(stat_file_handle);
	return Py_BuildValue("i", 0);
  }
  fclose(stat_file_handle);
  
  if(pid==1)
	return Py_BuildValue("i", 0); // set this explicitly. 
					     // I am not sure that ppid of init proccess is always 0
  
  return Py_BuildValue("i", parent_pid);
}

static PyMethodDef proc_helpers_methods[] = {
	{"getparentpid", py_getparentpid, METH_VARARGS},
	{NULL, NULL}
};

void initproc_helpers()
{
	(void) Py_InitModule("proc_helpers", proc_helpers_methods);
}
