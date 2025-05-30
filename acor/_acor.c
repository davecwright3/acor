/*
 * Written by: Dan Foreman-Mackey - danfm@nyu.edu
 */

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <numpy/arrayobject.h>

#include "acor.h"

struct module_state {
    PyObject *error;
};

/* Docstrings */
static char doc[] = "A module to estimate the autocorrelation time of time-series "\
                    "data extremely quickly.\n";

static char acor_doc[] =
"Estimate the autocorrelation time of a time series\n\n"\
"Parameters\n"\
"----------\n"\
"data : numpy.ndarray (N,) or (M, N)\n"\
"    The time series.\n\n"\
"maxlag : int\n"\
"    N must be greater than maxlag times the estimated autocorrelation\n"\
"    time.\n\n"\
"Returns\n"\
"-------\n"\
"tau : float\n"\
"    An estimate of the autocorrelation time.\n\n"\
"mean : float\n"\
"    The sample mean of data.\n\n"\
"sigma : float\n"\
"    An estimate of the standard deviation of the sample mean.\n\n"\
"Notes\n"\
"-----\n"\
"This is a _destructive_ operation! The first time series in data will be\n"\
"overwritten by the mean time series.\n\n";

static char function_doc[] =
"Calculate the autocorrelation function of a time series\n\n";

static PyObject *acor_acor(PyObject *self, PyObject *args)
{
    int i, j, N, ndim, info, maxlag;
    double *data;
    PyArrayObject *data_array = NULL;

    /* Return value */
    PyObject *ret;
    npy_intp M[1];
    double tau, mean, sigma;

    /* Parse the input tuple */
    PyObject *data_obj;
    if (!PyArg_ParseTuple(args, "Oi", &data_obj, &maxlag))
        return NULL;

    /* Get the data as a numpy array object */
    data_array  = (PyArrayObject *)PyArray_FROM_OTF(data_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY);
    if (data_array == NULL) {
        PyErr_SetString(PyExc_TypeError, "The input data must be a numpy.ndarray.");
        Py_XDECREF(data_array);
        return NULL;
    }

    /* Check the number of dimensions in the input data (must be 1 or 2 only) */
    ndim = (int)PyArray_NDIM(data_array);
    if (ndim > 2 || ndim < 1) {
        PyErr_SetString(PyExc_TypeError, "The input data must be a 1- or 2-D numpy.ndarray.");
        Py_DECREF(data_array);
        return NULL;
    }

    /* Get a pointer to the input data */
    data = (double*)PyArray_DATA(data_array);

    /* N gives the length of the time series */
    N = (int)PyArray_DIM(data_array, ndim-1);

    /* The zeroth (and only) element of M gives the number of series (default to 1) */
    M[0] = 1;
    if (ndim == 2)
        M[0] = (int)PyArray_DIM(data_array, 0);

    /* Take the mean of the chains at each time step */
    /* This is a *destructive* operation! */
    if (M[0] > 1) {
        for (i = 1; i < M[0]; i++) {
            for (j = 0; j < N; j++)
                data[j] += data[i*N+j];
        }
        for (j = 0; j < N; j++)
            data[j] /= (double)(M[0]);
    }

    info = acor(&mean, &sigma, &tau, data, N, maxlag);
    if (info != 0) {
        switch (info) {
            case 1:
                PyErr_Format(PyExc_RuntimeError, "The autocorrelation time is too "\
                    "long relative to the variance in dimension %d.", i+1);
                break;
            case -1:
                PyErr_SetString(PyExc_RuntimeError, "Couldn't allocate memory for "\
                    "autocovariance vector.");
                break;
            case 2:
                PyErr_SetString(PyExc_RuntimeError, "D was negative in acor. "\
                    "Can't calculate sigma.");
                break;
            default:
                PyErr_SetString(PyExc_RuntimeError, "acor failed.");
        }

        Py_DECREF(data_array);
        return NULL;
    }

    /* clean up */
    Py_DECREF(data_array);

    /* Build the output tuple */
    ret = Py_BuildValue("ddd", tau, mean, sigma);
    if (ret == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Couldn't build output tuple.");
        return NULL;
    }

    return ret;
}


static PyObject *acor_function(PyObject *self, PyObject *args)
{
    int ndim, ndim_out, N, N_out;
    double *data, *out_data;
    double mean;
    PyArrayObject *data_array = NULL;
    PyArrayObject *out_array = NULL;

    /* Parse the input tuple */
    PyObject *data_obj, *out_obj;
    if (!PyArg_ParseTuple(args, "OO", &data_obj, &out_obj))
        return NULL;

    /* Get the data as a numpy array object */
    data_array = (PyArrayObject *)PyArray_FROM_OTF(data_obj, NPY_DOUBLE, NPY_ARRAY_IN_ARRAY);
    if (data_array == NULL) {
        return NULL;
    }

    out_array = (PyArrayObject *)PyArray_FROM_OTF(out_obj, NPY_DOUBLE, NPY_ARRAY_OUT_ARRAY);
    if (out_array == NULL) {
        Py_DECREF(data_array);
        return NULL;
    }

    /* Check the number of dimensions in the input data (must be 1 or 2 only) */
    ndim = (int)PyArray_NDIM(data_array);
    ndim_out = (int)PyArray_NDIM(out_array);
    if (ndim != 1 || ndim_out != 1) {
        PyErr_SetString(PyExc_TypeError,
                "The input/output arrays must be a 1D numpy.ndarray.");
        Py_DECREF(data_array);
        Py_DECREF(out_array);
        return NULL;
    }

    /* Get a pointer to the input data */
    data = (double *)PyArray_DATA(data_array);
    out_data = (double *)PyArray_DATA(out_array);

    /* N gives the length of the time series */
    N = (int)PyArray_DIM(data_array, 0);
    N_out = (int)PyArray_DIM(out_array, 0);

    /* Make sure that the lengths are compatible */
    if (N_out > N) {
        PyErr_SetString(PyExc_TypeError, "The array lengths are not sane.");
        Py_DECREF(data_array);
        Py_DECREF(out_array);
        return NULL;
    }

    acor_fn(&mean, out_data, data, N, N_out);

    /* clean up */
    Py_DECREF(data_array);
    Py_DECREF(out_array);

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef acor_methods[] = {
    {"acor", acor_acor, METH_VARARGS, acor_doc},
    {"function", acor_function, METH_VARARGS, function_doc},
    {NULL, NULL, 0, NULL}
};

#if PY_MAJOR_VERSION >= 3

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static int acor_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int acor_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "_acor",
    doc,
    sizeof(struct module_state),
    acor_methods,
    NULL,
    acor_traverse,
    acor_clear,
    NULL
};

#define INITERROR return NULL

PyObject *PyInit__acor(void)
#else

#define INITERROR return

void init_acor(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule3("_acor", acor_methods, doc);
#endif

    if (module == NULL)
        INITERROR;
    import_array();

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}
