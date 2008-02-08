/*
   Copyright (c) 2007 Chris AtLee <chris@atlee.ca>
   This file is part of GlusterFS.

   GlusterFS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3 of the License,
   or (at your option) any later version.

   GlusterFS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see
   <http://www.gnu.org/licenses/>.
*/
#include <Python.h>

#include "glusterfs.h"
#include "xlator.h"
#include "logging.h"
#include "defaults.h"

typedef struct
{
    char        *scriptname;
    PyObject    *pXlator;
    PyObject    *pScriptModule;
    PyObject    *pGlusterModule;
    PyThreadState *pInterp;

    PyObject    *pFrameType, *pVectorType, *pFdType;
} python_private_t;

int32_t
python_writev (call_frame_t *frame,
              xlator_t *this,
              fd_t *fd,
              struct iovec *vector,
              int32_t count, 
              off_t offset)
{
  python_private_t *priv = (python_private_t *)this->private;
  gf_log("python", GF_LOG_DEBUG, "In writev");
  if (PyObject_HasAttrString(priv->pXlator, "writev"))
  {

      PyObject *retval = PyObject_CallMethod(priv->pXlator, "writev",
              "O O O i l",
              PyObject_CallMethod(priv->pFrameType, "from_address", "O&", PyLong_FromVoidPtr, frame),
              PyObject_CallMethod(priv->pFdType, "from_address", "O&", PyLong_FromVoidPtr, fd),
              PyObject_CallMethod(priv->pVectorType, "from_address", "O&", PyLong_FromVoidPtr, vector),
              count,
              offset);
      if (PyErr_Occurred())
      {
          PyErr_Print();
      }
      Py_XDECREF(retval);
  }
  else
  {
      return default_writev(frame, this, fd, vector, count, offset);
  }
  return 0;
}

struct xlator_fops fops = {
    .writev       = python_writev
};

struct xlator_mops mops = {
};

static PyObject *
AnonModule_FromFile (const char* fname)
{
    // Get the builtins
    PyThreadState* pThread = PyThreadState_Get();
    PyObject *pBuiltins = pThread->interp->builtins;

    if (PyErr_Occurred())
    {
        PyErr_Print();
        return NULL;
    }

    // Create a new dictionary for running code in
    PyObject *pModuleDict = PyDict_New();
    PyDict_SetItemString(pModuleDict, "__builtins__", pBuiltins);
    Py_INCREF(pBuiltins);

    // Run the file in the new context
    FILE* fp = fopen(fname, "r");
    PyRun_File(fp, fname, Py_file_input, pModuleDict, pModuleDict);
    fclose(fp);
    if (PyErr_Occurred())
    {
        PyErr_Print();
        Py_DECREF(pModuleDict);
        Py_DECREF(pBuiltins);
        return NULL;
    }

    // Create an object to hold the new context
    PyRun_String("class ModuleWrapper(object):\n\tpass\n", Py_single_input, pModuleDict, pModuleDict);
    if (PyErr_Occurred())
    {
        PyErr_Print();
        Py_DECREF(pModuleDict);
        Py_DECREF(pBuiltins);
        return NULL;
    }
    PyObject *pModule = PyRun_String("ModuleWrapper()", Py_eval_input, pModuleDict, pModuleDict);
    if (PyErr_Occurred())
    {
        PyErr_Print();
        Py_DECREF(pModuleDict);
        Py_DECREF(pBuiltins);
        Py_XDECREF(pModule);
        return NULL;
    }

    // Set the new context's dictionary to the one we used to run the code
    // inside
    PyObject_SetAttrString(pModule, "__dict__", pModuleDict);
    if (PyErr_Occurred())
    {
        PyErr_Print();
        Py_DECREF(pModuleDict);
        Py_DECREF(pBuiltins);
        Py_DECREF(pModule);
        return NULL;
    }

    return pModule;
}

int32_t
init (xlator_t *this)
{
  // This is ok to call more than once per process
  Py_InitializeEx(0);

  if (!this->children) {
    gf_log ("python", GF_LOG_ERROR, 
            "FATAL: python should have exactly one child");
    return -1;
  }

  python_private_t *priv = calloc (sizeof (python_private_t), 1);

  data_t *scriptname = dict_get (this->options, "scriptname");
  if (scriptname) {
      priv->scriptname = data_to_str(scriptname);
  } else {
      gf_log("python", GF_LOG_ERROR,
              "FATAL: python requires the scriptname parameter");
      return -1;
  }

  priv->pInterp = Py_NewInterpreter();
    
  // Adjust python's path
  PyObject *syspath = PySys_GetObject("path");
  PyObject *path = PyString_FromString(GLUSTER_PYTHON_PATH);
  PyList_Append(syspath, path);
  Py_DECREF(path);

  gf_log("python", GF_LOG_DEBUG,
          "Loading gluster module");

  priv->pGlusterModule = PyImport_ImportModule("gluster");
  if (PyErr_Occurred())
  {
      PyErr_Print();
      return -1;
  }

  priv->pFrameType = PyObject_GetAttrString(priv->pGlusterModule, "call_frame_t");
  priv->pFdType = PyObject_GetAttrString(priv->pGlusterModule, "fd_t");
  priv->pVectorType = PyObject_GetAttrString(priv->pGlusterModule, "iovec");

  gf_log("python", GF_LOG_DEBUG, "Loading script...%s", priv->scriptname);
  
  priv->pScriptModule = AnonModule_FromFile(priv->scriptname);
  if (!priv->pScriptModule || PyErr_Occurred())
  {
      gf_log("python", GF_LOG_ERROR, "Error loading %s", priv->scriptname);
      PyErr_Print();
      return -1;
  }

  if (!PyObject_HasAttrString(priv->pScriptModule, "xlator"))
  {
      gf_log("python", GF_LOG_ERROR, "%s does not have a xlator attribute", priv->scriptname);
      return -1;
  }
  gf_log("python", GF_LOG_DEBUG, "Instantiating translator");
  priv->pXlator = PyObject_CallMethod(priv->pScriptModule, "xlator", "O&",
          PyLong_FromVoidPtr, this);
  if (PyErr_Occurred() || !priv->pXlator)
  {
      PyErr_Print();
      return -1;
  }

  this->private = priv;

  gf_log ("python", GF_LOG_DEBUG, "python xlator loaded");
  return 0;
}

void 
fini (xlator_t *this)
{
  python_private_t *priv = (python_private_t*)(this->private);
  Py_DECREF(priv->pXlator);
  Py_DECREF(priv->pScriptModule);
  Py_DECREF(priv->pGlusterModule);
  Py_DECREF(priv->pFrameType);
  Py_DECREF(priv->pFdType);
  Py_DECREF(priv->pVectorType);
  Py_EndInterpreter(priv->pInterp);
  return;
}
