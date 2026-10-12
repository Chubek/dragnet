%module limestone
%{
#include "limestone/limestone.h"
#include "limestone/il.h"
#include "limestone/runtime.h"
#include "limestone/optimization.h"
#include "limestone/object.h"
#include "limestone/traceml.h"
#include <cstring>
#include <cstdio>
#include <new>
#include <vector>
%}
%include <stdint.i>
%include <typemaps.i>
%include <carrays.i>
%array_class(uint32_t, UInt32Array);
%array_class(uint8_t, ByteArray);
%typemap(in, numinputs=0) uint32_t *OUTPUT (uint32_t temp=0) {
  $1 = &temp;
}
%typemap(argout) uint32_t *OUTPUT {
  $result = SWIG_AppendOutput($result, PyLong_FromUnsignedLong(*$1));
}
%typemap(in, numinputs=0) int64_t *OUTPUT (int64_t temp=0) {
  $1 = &temp;
}
%typemap(argout) int64_t *OUTPUT {
  $result = SWIG_AppendOutput($result, PyLong_FromLongLong(*$1));
}
%typemap(in, numinputs=0) size_t *OUTPUT (size_t temp=0) {
  $1 = &temp;
}
%typemap(argout) size_t *OUTPUT {
  $result = SWIG_AppendOutput($result, PyLong_FromSize_t(*$1));
}
%typemap(in, numinputs=0) int32_t *OUTPUT (int32_t temp=0) { $1 = &temp; }
%typemap(argout) int32_t *OUTPUT { $result = SWIG_AppendOutput($result, PyLong_FromLong(*$1)); }
%apply int32_t *OUTPUT {int32_t *literal};
%apply uint32_t *OUTPUT {uint32_t *id, uint32_t *block, uint32_t *physical, uint32_t *value, uint32_t *implicit_addend};
%apply int64_t *OUTPUT {int64_t *result};
%apply size_t *OUTPUT {size_t *invalidated};
%typemap(in, numinputs=0) double *OUTPUT (double temp=0) { $1 = &temp; }
%typemap(argout) double *OUTPUT { $result = SWIG_AppendOutput($result, PyFloat_FromDouble(*$1)); }
%apply double *OUTPUT {double *cost};
%typemap(memberin) const char *address_space {
  char *copy = 0;
  if ($input) {
    size_t size = strlen($input) + 1;
    copy = new (std::nothrow) char[size];
    if (!copy) SWIG_exception_fail(SWIG_MemoryError, "unable to copy address space");
    memcpy(copy, $input, size);
  }
  delete[] $1;
  $1 = copy;
}
// The C descriptor borrows this pointer; setting it must not disown the array.
%typemap(in) const uint32_t *alias_sets {
  void *pointer = 0;
  int status = SWIG_ConvertPtr($input, &pointer, $descriptor(uint32_t *), 0);
  if (!SWIG_IsOK(status)) SWIG_exception_fail(SWIG_TypeError, "expected a uint32_t array");
  $1 = static_cast<uint32_t *>(pointer);
}
%immutable limestone_spill_slot::register_class;
%immutable limestone_group_info::name;
%immutable limestone_group_info::origin;
%immutable limestone_binary_semantic_view::semantics;
%immutable limestone_group_info::pattern;
%immutable limestone_burs_state::nonterminal_name;
%immutable limestone_burs_attempt::reason;
%immutable limestone_match_info::name;
%immutable limestone_match_info::opcode;
%immutable limestone_match_info::origin;
%immutable limestone_match_info::reason;
%immutable limestone_runtime_region_view::bytes;
%immutable limestone_runtime_region_view::guest_bytes;
%include "limestone/limestone.h"
%include "limestone/il.h"
%include "limestone/runtime.h"
%include "limestone/optimization.h"
%typemap(in) (const limestone_traceml_value *const *arguments, size_t count) (std::vector<limestone_traceml_value *> items) {
  if (!PyList_Check($input) && !PyTuple_Check($input)) SWIG_exception_fail(SWIG_TypeError, "expected TraceLambda value handles in a list or tuple");
  Py_ssize_t length = PySequence_Size($input);
  if (length < 0 || length > 65536) SWIG_exception_fail(SWIG_ValueError, "TraceLambda argument count outside [0,65536]");
  try { items.resize(static_cast<size_t>(length)); } catch (...) { SWIG_exception_fail(SWIG_MemoryError, "TraceLambda argument allocation failed"); }
  for (Py_ssize_t k = 0; k < length; ++k) {
    PyObject *item = PySequence_GetItem($input, k); void *pointer = 0;
    int status = item ? SWIG_ConvertPtr(item, &pointer, $descriptor(limestone_traceml_value *), 0) : SWIG_ERROR;
    Py_XDECREF(item);
    if (!SWIG_IsOK(status) || !pointer) SWIG_exception_fail(SWIG_TypeError, "expected a non-null TraceLambda value handle");
    items[static_cast<size_t>(k)] = static_cast<limestone_traceml_value *>(pointer);
  }
  $1 = items.data(); $2 = items.size();
}
%include "limestone/traceml.h"
%typemap(in, numinputs=0) uint64_t *OUTPUT (uint64_t temp=0) { $1 = &temp; }
%typemap(argout) uint64_t *OUTPUT { $result = SWIG_AppendOutput($result, PyLong_FromUnsignedLongLong(*$1)); }
%apply uint64_t *OUTPUT {uint64_t *address};
%typemap(in) (const uint8_t *bytes, size_t size) (char *data=0, Py_ssize_t length=0) {
  if (PyBytes_AsStringAndSize($input, &data, &length) < 0) SWIG_fail;
  $1 = reinterpret_cast<uint8_t *>(data); $2 = static_cast<size_t>(length);
}
%typemap(in) (const limestone_object *const *objects, size_t count) (std::vector<limestone_object *> items) {
  if (!PyList_Check($input) && !PyTuple_Check($input)) SWIG_exception_fail(SWIG_TypeError, "expected object handles in a list or tuple");
  Py_ssize_t length = PySequence_Size($input);
  if (length < 1 || length > 16384) SWIG_exception_fail(SWIG_ValueError, "object count outside [1,16384]");
  try { items.resize(static_cast<size_t>(length)); } catch (...) { SWIG_exception_fail(SWIG_MemoryError, "object handle allocation failed"); }
  for (Py_ssize_t k = 0; k < length; ++k) {
    PyObject *item = PySequence_GetItem($input, k); void *pointer = 0;
    int status = item ? SWIG_ConvertPtr(item, &pointer, $descriptor(limestone_object *), 0) : SWIG_ERROR;
    Py_XDECREF(item);
    if (!SWIG_IsOK(status)) SWIG_exception_fail(SWIG_TypeError, "expected an object handle");
    items[static_cast<size_t>(k)] = static_cast<limestone_object *>(pointer);
  }
  $1 = items.data(); $2 = items.size();
}
%typemap(in) (const limestone_external_symbol *externals, size_t external_count) (std::vector<limestone_external_symbol> items) {
  if (!PyDict_Check($input)) SWIG_exception_fail(SWIG_TypeError, "expected an external-symbol dictionary");
  Py_ssize_t size = PyDict_Size($input), position = 0; PyObject *key, *value;
  if (size > 1048576) SWIG_exception_fail(SWIG_ValueError, "external symbol count limit exceeded");
  try { items.reserve(static_cast<size_t>(size)); } catch (...) { SWIG_exception_fail(SWIG_MemoryError, "symbol allocation failed"); }
  while (PyDict_Next($input, &position, &key, &value)) {
    Py_ssize_t length = 0;
    const char *name = PyUnicode_Check(key) ? PyUnicode_AsUTF8AndSize(key, &length) : NULL;
    if (!name) SWIG_exception_fail(SWIG_TypeError, "external symbol names must be strings");
    if (!length || std::memchr(name, 0, static_cast<size_t>(length))) SWIG_exception_fail(SWIG_ValueError, "invalid external symbol name");
    unsigned long long address = PyLong_AsUnsignedLongLong(value); if (PyErr_Occurred()) SWIG_fail;
    items.push_back({name, static_cast<uint64_t>(address)});
  }
  $1 = items.data(); $2 = items.size();
}
%typemap(in) (const limestone_archive *const *archives, size_t archive_count) (std::vector<limestone_archive *> items) {
  if (!PyList_Check($input) && !PyTuple_Check($input)) SWIG_exception_fail(SWIG_TypeError, "expected archive handles in a list or tuple");
  Py_ssize_t length = PySequence_Size($input);
  if (length < 0 || length > 4096) SWIG_exception_fail(SWIG_ValueError, "archive count outside [0,4096]");
  try { items.resize(static_cast<size_t>(length)); } catch (...) { SWIG_exception_fail(SWIG_MemoryError, "archive handle allocation failed"); }
  for (Py_ssize_t k = 0; k < length; ++k) {
    PyObject *item = PySequence_GetItem($input, k); void *pointer = 0;
    int status = item ? SWIG_ConvertPtr(item, &pointer, $descriptor(limestone_archive *), 0) : SWIG_ERROR;
    Py_XDECREF(item);
    if (!SWIG_IsOK(status)) SWIG_exception_fail(SWIG_TypeError, "expected an archive handle");
    items[static_cast<size_t>(k)] = static_cast<limestone_archive *>(pointer);
  }
  $1 = items.data(); $2 = items.size();
}
%immutable limestone_object_section::name;
%immutable limestone_object_section::bytes;
%immutable limestone_object_symbol::name;
%immutable limestone_external_symbol::name;
%include "limestone/object.h"
%extend limestone_memory_access {
  ~limestone_memory_access() {
    delete[] $self->address_space;
    delete $self;
  }
}
%typemap(out) PyObject * {
  $result = $1;
  if (!$result) SWIG_fail;
}
%typemap(in) PyObject *callback { $1=$input; }
%ignore limestone_python_selection_release;
%ignore limestone_python_selection_proof;
%inline %{
static void limestone_python_selection_release(void *userdata) {
  PyGILState_STATE state=PyGILState_Ensure();Py_DECREF(static_cast<PyObject *>(userdata));PyGILState_Release(state);
}
static limestone_status limestone_python_selection_proof(const char *context,const char *parameters,
    int *proved,void *userdata,limestone_error *error) {
  PyGILState_STATE state=PyGILState_Ensure();
  PyObject *result=PyObject_CallFunction(static_cast<PyObject *>(userdata),"ss",context,parameters);
  limestone_status status=LIMESTONE_OK;
  if(!result){
    PyObject *type=NULL,*value=NULL,*trace=NULL;PyErr_Fetch(&type,&value,&trace);
    PyObject *message=value?PyObject_Str(value):NULL;const char *text=message?PyUnicode_AsUTF8(message):NULL;
    error->code=LIMESTONE_INTERNAL;std::snprintf(error->message,sizeof(error->message),"Python selection proof: %s",text?text:"callback failed");
    Py_XDECREF(message);Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(trace);PyErr_Clear();status=LIMESTONE_INTERNAL;
  }else if(!PyBool_Check(result)){error->code=LIMESTONE_INVALID_ARGUMENT;std::snprintf(error->message,sizeof(error->message),"selection proof must return bool");status=LIMESTONE_INVALID_ARGUMENT;}
  else *proved=result==Py_True;
  Py_XDECREF(result);PyGILState_Release(state);return status;
}
static limestone_selection_predicate *limestone_python_selection_predicate(const char *name,PyObject *callback,limestone_error *error) {
  if(!PyCallable_Check(callback)){error->code=LIMESTONE_INVALID_ARGUMENT;std::snprintf(error->message,sizeof(error->message),"selection proof must be callable");return NULL;}
  Py_INCREF(callback);auto result=limestone_selection_predicate_create(name,limestone_python_selection_proof,callback,limestone_python_selection_release,error);
  if(!result)Py_DECREF(callback);return result;
}
static size_t limestone_allocation_ranges_index(void) { return LIMESTONE_ALLOCATION_RANGES; }
static PyObject *limestone_module_bytes_copy(const limestone_module *module) {
  return PyBytes_FromStringAndSize((const char *)limestone_module_bytes(module),
    (Py_ssize_t)limestone_module_byte_count(module));
}
static PyObject *limestone_buffer_bytes_copy(const limestone_buffer *buffer) {
  return PyBytes_FromStringAndSize((const char *)limestone_buffer_data(buffer),
    (Py_ssize_t)limestone_buffer_size(buffer));
}
static PyObject *limestone_object_data_bytes_copy(const limestone_object_data *data) {
  return PyBytes_FromStringAndSize((const char *)limestone_object_data_bytes(data), (Py_ssize_t)limestone_object_data_size(data));
}
static PyObject *limestone_linked_image_bytes_copy(const limestone_linked_image *image) {
  return PyBytes_FromStringAndSize((const char *)limestone_linked_image_bytes(image), (Py_ssize_t)limestone_linked_image_size(image));
}
static PyObject *limestone_object_section_bytes_copy(const limestone_object *object, size_t index) {
  limestone_object_section section; limestone_error error;
  if (limestone_object_get_section(object, index, &section, &error) != LIMESTONE_OK) { PyErr_SetString(PyExc_ValueError, error.message); return NULL; }
  return PyBytes_FromStringAndSize((const char *)section.bytes, (Py_ssize_t)section.byte_count);
}
static limestone_status limestone_python_object_add_section(limestone_object *object, const char *name, uint32_t type,
    uint64_t flags, uint64_t alignment, const uint8_t *bytes, size_t size, uint64_t zero_fill, uint32_t *id, limestone_error *error) {
  limestone_object_section section = {name,type,flags,alignment,0,bytes,size,zero_fill};
  return limestone_object_add_section(object, &section, id, error);
}
static limestone_status limestone_python_object_add_symbol(limestone_object *object, const char *name, uint32_t section,
    uint64_t value, uint64_t size, limestone_symbol_binding binding, uint8_t type, uint8_t visibility, uint32_t *id, limestone_error *error) {
  limestone_object_symbol symbol = {name,section,value,size,binding,type,visibility};
  return limestone_object_add_symbol(object, &symbol, id, error);
}
static PyObject *limestone_translated_region_bytes_copy(const limestone_translated_region *region, int guest) {
  limestone_runtime_region_view view;
  limestone_error error;
  if (limestone_translated_region_get_view(region, &view, &error) != LIMESTONE_OK) {
    PyErr_SetString(PyExc_ValueError, error.message);
    return NULL;
  }
  return PyBytes_FromStringAndSize((const char *)(guest ? view.guest_bytes : view.bytes),
    (Py_ssize_t)(guest ? view.guest_size : view.byte_count));
}
%}
%pythoncode %{
LIMESTONE_ALLOCATION_RANGES = limestone_allocation_ranges_index()
def _set_alias_sets(self, values):
    """Copy alias identities and retain their array for the descriptor lifetime."""
    array = UInt32Array(len(values))
    for index, value in enumerate(values):
        array[index] = value
    self.alias_sets = array.cast()
    self.alias_count = len(values)
    self._alias_array = array


limestone_memory_access.set_alias_sets = _set_alias_sets


class LimestoneError(RuntimeError):
    """A structured diagnostic from the C ABI."""
    def __init__(self, error):
        self.code = error.code
        super().__init__(error.message)


def _checked_create(function, *args):
    error = limestone_error()
    handle = function(*args, error)
    if not handle:
        raise LimestoneError(error)
    return handle


class _Handle:
    """Unique ownership; context managers close deterministically."""
    def __init__(self, handle, destroy):
        if not handle:
            raise MemoryError("unable to create Limestone handle")
        self._handle, self._destroy = handle, destroy

    def _pointer(self):
        if self._handle is None:
            raise ValueError("Limestone handle is closed")
        return self._handle

    def close(self):
        handle, self._handle = self._handle, None
        if handle is not None:
            self._destroy(handle)

    def __enter__(self):
        self._pointer()
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        if getattr(self, "_handle", None) is not None:
            self.close()

    def __copy__(self):
        raise TypeError("Limestone handles have unique ownership")

    def __deepcopy__(self, memo):
        raise TypeError("Limestone handles have unique ownership")


class SelectionPredicate(_Handle):
    """Owning deterministic proof over copied root/bindings/covered dictionaries."""
    def __init__(self, name, callback):
        import json
        if not callable(callback):
            raise TypeError("selection proof must be callable")
        def prove(context, parameters):
            return callback(json.loads(context), json.loads(parameters))
        super().__init__(_checked_create(limestone_python_selection_predicate, name, prove), limestone_selection_predicate_destroy)
        self.name = name

    def attach_target(self, target):
        error = limestone_error()
        if limestone_target_set_selection_predicate(target, self.name, self._pointer(), error) != LIMESTONE_OK:
            raise LimestoneError(error)


def _attach_selection_predicate(function, document, predicate):
    error = limestone_error()
    if function(document, predicate.name, predicate._pointer(), error) != LIMESTONE_OK:
        raise LimestoneError(error)


class Module(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_module_destroy)

    @property
    def text(self):
        return limestone_module_text(self._pointer())

    @property
    def exchange(self):
        return limestone_module_exchange(self._pointer())

    @property
    def bytes(self):
        return limestone_module_bytes_copy(self._pointer())

    @property
    def stages(self):
        p = self._pointer()
        return [limestone_module_stage(p, k) for k in range(limestone_module_stage_count(p))]


def compile_source(source, options=None):
    """Compile a closed TraceML integer program to portable MachineIR."""
    return Module(_checked_create(limestone_compile_checked, source, options))


def compile_umd(source, options=None):
    return Module(_checked_create(limestone_compile_umd, source, options))


def compile_target(source, target, configuration=None):
    """Closed TraceML lowering through a raw target/configuration handle."""
    return Module(_checked_create(limestone_compile_target, source, target, configuration))


def compile_umd_file(path, options=None):
    """Load a UMD file with bounded, source-located relative includes."""
    return Module(_checked_create(limestone_compile_umd_file, str(path), options))


def trace_options(step_limit=100000, event_limit=100000, record_trace=True, record_guards=False):
    options = limestone_traceml_options()
    limestone_traceml_options_default(options)
    options.step_limit, options.event_limit = step_limit, event_limit
    options.record_trace, options.record_guards = int(record_trace), int(record_guards)
    return options


def _trace_arguments(arguments):
    if not isinstance(arguments, (list, tuple)):
        raise TypeError("TraceLambda arguments must be a list or tuple")
    if len(arguments) > 65536:
        raise ValueError("TraceLambda argument count exceeds 65536")
    values = [value if isinstance(value, TraceValue) else TraceValue(value) for value in arguments]
    return values, [value._pointer() for value in values]


class TraceValue(_Handle):
    """Owning integer or immutable lexical closure; independent of its result."""
    def __init__(self, integer=0, *, handle=None):
        if handle is None:
            handle = _checked_create(limestone_traceml_integer, integer)
        super().__init__(handle, limestone_traceml_value_destroy)

    @property
    def callable(self):
        return bool(limestone_traceml_value_callable(self._pointer()))

    @property
    def integer(self):
        error = limestone_error()
        status, value = limestone_traceml_value_integer(self._pointer(), error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return value

    def apply(self, arguments=(), options=None):
        values, handles = _trace_arguments(arguments)
        return TraceResult(_checked_create(limestone_traceml_apply, self._pointer(), handles, options))


class TraceGuard(_Handle):
    """Owning guard safepoint, including lexical and continuation state."""
    def __init__(self, handle):
        super().__init__(handle, limestone_traceml_guard_destroy)

    @property
    def expected(self):
        return bool(limestone_traceml_guard_expected(self._pointer()))

    def resume(self, condition, options=None):
        return TraceResult(_checked_create(limestone_traceml_resume, self._pointer(), condition, options))


class TraceResult(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_traceml_result_destroy)

    @property
    def value(self):
        return TraceValue(handle=_checked_create(limestone_traceml_result_value, self._pointer()))

    @property
    def trace(self):
        return limestone_traceml_result_trace(self._pointer())

    @property
    def guards(self):
        pointer = self._pointer()
        return [TraceGuard(_checked_create(limestone_traceml_result_guard, pointer, index))
                for index in range(limestone_traceml_result_guard_count(pointer))]


class TraceRuntime(_Handle):
    """Checked owning TraceLambda runtime; calls preserve call-by-name semantics."""
    def __init__(self, source, node_limit=100000):
        super().__init__(_checked_create(limestone_traceml_prepare, source, node_limit), limestone_traceml_runtime_destroy)

    def invoke(self, arguments=(), options=None):
        values, handles = _trace_arguments(arguments)
        return TraceResult(_checked_create(limestone_traceml_invoke, self._pointer(), handles, options))


def _matches(pointer, count, inspect, values):
    result = []
    for index in range(count):
        info, error = limestone_match_info(), limestone_error()
        if inspect(pointer, index, info, error) != LIMESTONE_OK:
            raise LimestoneError(error)
        match = dict(pattern=info.pattern, root=info.root, cost=info.cost,
                     name=info.name, opcode=info.opcode, origin=info.origin, reason=info.reason)
        for role, key, size in ((LIMESTONE_MATCH_COVERED, "covered", info.covered_count),
                                (LIMESTONE_MATCH_INPUT, "inputs", info.input_count),
                                (LIMESTONE_MATCH_OUTPUT, "outputs", info.output_count)):
            entries = []
            for k in range(size):
                status, value = values(pointer, index, role, k, error)
                if status != LIMESTONE_OK:
                    raise LimestoneError(error)
                entries.append(value)
            match[key] = entries
        result.append(match)
    return result


class Selection(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_selection_destroy)

    @property
    def cost(self):
        return limestone_selection_cost(self._pointer())

    @property
    def text(self):
        return limestone_selection_text(self._pointer())

    @property
    def matches(self):
        pointer = self._pointer()
        return _matches(pointer, limestone_selection_count(pointer),
                        limestone_selection_candidate, limestone_selection_value)


class SelectionModel(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_selection_model_destroy)

    @property
    def text(self):
        return limestone_selection_model_text(self._pointer())

    @property
    def candidates(self):
        pointer = self._pointer()
        return _matches(pointer, limestone_selection_model_candidate_count(pointer),
                        limestone_selection_model_candidate, limestone_selection_model_value)

    @property
    def clauses(self):
        pointer, error = self._pointer(), limestone_error()
        result = []
        for clause in range(limestone_selection_model_clause_count(pointer)):
            entries = []
            for k in range(limestone_selection_model_literal_count(pointer, clause)):
                status, literal = limestone_selection_model_literal(pointer, clause, k, error)
                if status != LIMESTONE_OK:
                    raise LimestoneError(error)
                entries.append(literal)
            result.append(entries)
        return result

    def select(self, algorithm=LIMESTONE_SELECT_GLOBAL):
        return Selection(_checked_create(limestone_selection_run, self._pointer(), algorithm))


class UniselDocument(_Handle):
    def __init__(self, source, source_name=None):
        super().__init__(_checked_create(limestone_unisel_load, source, source_name), limestone_unisel_document_destroy)

    @classmethod
    def from_file(cls, path):
        document = cls.__new__(cls)
        _Handle.__init__(document, _checked_create(limestone_unisel_load_file, str(path)), limestone_unisel_document_destroy)
        return document

    def analyze(self):
        return SelectionModel(_checked_create(limestone_unisel_analyze, self._pointer()))

    def attach_predicate(self, predicate):
        _attach_selection_predicate(limestone_unisel_set_selection_predicate, self._pointer(), predicate)


class BURSSelection(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_burs_selection_destroy)

    @property
    def text(self):
        return limestone_burs_selection_text(self._pointer())

    @property
    def cost(self):
        return limestone_burs_selection_cost(self._pointer())


class BURSDocument(_Handle):
    def __init__(self, source, source_name=None):
        super().__init__(_checked_create(limestone_burs_load, source, source_name), limestone_burs_document_destroy)

    @classmethod
    def from_file(cls, path):
        document = cls.__new__(cls)
        _Handle.__init__(document, _checked_create(limestone_burs_load_file, str(path)), limestone_burs_document_destroy)
        return document

    def select(self, tree=0):
        return BURSSelection(_checked_create(limestone_burs_select, self._pointer(), tree))

    def analyze(self, tree=0):
        return BURSAnalysis(_checked_create(limestone_burs_analyze, self._pointer(), tree))

    def attach_predicate(self, predicate):
        _attach_selection_predicate(limestone_burs_set_selection_predicate, self._pointer(), predicate)


class BURSAnalysis(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_burs_analysis_destroy)

    @property
    def states(self):
        result = []
        pointer = self._pointer()
        for index in range(limestone_burs_state_count(pointer)):
            state, error = limestone_burs_state(), limestone_error()
            if limestone_burs_state_at(pointer, index, state, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append({"node": state.node, "nonterminal": state.nonterminal,
                           "name": state.nonterminal_name, "rule": state.rule, "cost": state.cost})
        return result

    @property
    def attempts(self):
        result = []
        pointer = self._pointer()
        for index in range(limestone_burs_attempt_count(pointer)):
            attempt, error = limestone_burs_attempt(), limestone_error()
            if limestone_burs_attempt_at(pointer, index, attempt, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append({"node": attempt.node, "rule": attempt.rule,
                           "cost": attempt.cost if attempt.matched else None,
                           "improves_state": bool(attempt.improves_state), "reason": attempt.reason})
        return result


class Schedule(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_schedule_destroy)

    @property
    def text(self):
        return limestone_schedule_text(self._pointer())

    @property
    def slot_assignments(self):
        pointer, error = self._pointer(), limestone_error()
        result = []
        for index in range(limestone_schedule_count(pointer)):
            slots = []
            for slot in range(limestone_schedule_slot_count(pointer, index)):
                status, value = limestone_schedule_slot(pointer, index, slot, error)
                if status != LIMESTONE_OK:
                    raise LimestoneError(error)
                slots.append(value)
            result.append(slots)
        return result

    @property
    def issues(self):
        p = self._pointer()
        result = []
        for k in range(limestone_schedule_count(p)):
            issue, error = limestone_issue(), limestone_error()
            if limestone_schedule_issue(p, k, issue, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            resources = [limestone_schedule_resource(p, k, n) for n in range(limestone_schedule_resource_count(p, k))]
            result.append((issue.instruction, issue.cycle, issue.slot if issue.has_slot else None, resources))
        return result


    @property
    def groups(self):
        p, error = self._pointer(), limestone_error()
        result = []
        for k in range(limestone_schedule_group_count(p)):
            group = limestone_group_info()
            if limestone_schedule_group(p, k, group, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            members = []
            for n in range(group.member_count):
                status, member = limestone_schedule_group_member(p, k, n, error)
                if status != LIMESTONE_OK:
                    raise LimestoneError(error)
                members.append(member)
            result.append(dict(id=group.id, kind=group.kind, name=group.name,
                               origin=group.origin, pattern=group.pattern,
                               benefit=group.benefit, issue_width=group.issue_width,
                               members=members))
        return result


class SchedulingDocument(_Handle):
    def __init__(self, source, source_name=None):
        super().__init__(_checked_create(limestone_schedrow_load, source, source_name), limestone_scheduling_document_destroy)

    def schedule(self, region=0, initiation_interval=0):
        return Schedule(_checked_create(limestone_schedule_run, self._pointer(), region, initiation_interval))


class Assignment(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_assignment_destroy)

    @property
    def text(self):
        return limestone_assignment_text(self._pointer())

    @property
    def cost(self):
        error = limestone_error()
        status, cost = limestone_assignment_cost(self._pointer(), error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return cost

    @property
    def registers(self):
        p, error = self._pointer(), limestone_error()
        result = {}
        for k in range(limestone_assignment_count(p)):
            status, value, physical = limestone_assignment_at(p, k, error)
            if status != LIMESTONE_OK:
                raise LimestoneError(error)
            result[value] = physical
        return result

    @property
    def spilled(self):
        p, error = self._pointer(), limestone_error()
        result = []
        for k in range(limestone_assignment_spill_count(p)):
            status, value = limestone_assignment_spill(p, k, error)
            if status != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append(value)
        return result


class AllocationDocument(_Handle):
    def __init__(self, source, source_name=None):
        super().__init__(_checked_create(limestone_regtl_load, source, source_name), limestone_allocation_document_destroy)

    def allocate(self, unit=0, function=0, algorithm=LIMESTONE_ALLOCATE_LINEAR):
        if function is None:
            function = LIMESTONE_ALLOCATION_RANGES
        return Assignment(_checked_create(limestone_assignment_run, self._pointer(), unit, function, algorithm))


class BinaryArchitecture(_Handle):
    def __init__(self, source):
        super().__init__(_checked_create(limestone_binary_architecture_load, source), limestone_binary_architecture_destroy)

    def translate(self, data, target=None, source_address=0, target_address=0, transform=None):
        data = bytes(data)
        array = ByteArray(len(data))
        for index, value in enumerate(data):
            array[index] = value
        buffer = _checked_create(limestone_binary_translate_with_transform, self._pointer(),
                                 self._pointer() if target is None else target._pointer(),
                                 array.cast(), len(data), source_address, target_address,
                                 None if transform is None else transform._pointer())
        try:
            return limestone_buffer_bytes_copy(buffer)
        finally:
            limestone_buffer_destroy(buffer)

    def disassemble(self, data, address=0):
        data = bytes(data)
        array = ByteArray(len(data))
        for index, value in enumerate(data):
            array[index] = value
        buffer = _checked_create(limestone_binary_disassemble, self._pointer(),
                                 array.cast(), len(data), address)
        try:
            return limestone_buffer_text(buffer)
        finally:
            limestone_buffer_destroy(buffer)


class Optimization(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_optimization_destroy)

    @property
    def used_fallback(self):
        return bool(limestone_optimization_used_fallback(self._pointer()))

    @property
    def expression(self):
        return limestone_optimization_expression(self._pointer())

    @property
    def info(self):
        info, error = limestone_optimization_info(), limestone_error()
        if limestone_optimization_get_info(self._pointer(), info, error) != LIMESTONE_OK:
            raise LimestoneError(error)
        return dict(cost=info.cost, rewrites=info.rewrites, nodes=info.nodes,
                    classes=info.classes, iterations=info.iterations,
                    saturated=bool(info.saturated), limit_reached=bool(info.limit_reached))

    @property
    def trace(self):
        pointer = self._pointer()
        return [limestone_optimization_trace(pointer, k)
                for k in range(limestone_optimization_trace_count(pointer))]


class Optimizer(_Handle):
    """Owning Tunah rules/costs/budgets. Host predicates use the raw C callback ABI."""
    def __init__(self, rules=None, source_name=None):
        super().__init__(_checked_create(limestone_optimizer_create), limestone_optimizer_destroy)
        if rules is not None:
            self.load_rules(rules, source_name)

    def _call(self, function, *arguments):
        error = limestone_error()
        if function(self._pointer(), *arguments, error) != LIMESTONE_OK:
            raise LimestoneError(error)

    def load_rules(self, rules, source_name=None):
        self._call(limestone_optimizer_load_rules, rules, source_name)

    def load_rules_file(self, path):
        self._call(limestone_optimizer_load_rules_file, str(path))

    def define_operator(self, name, arity):
        self._call(limestone_optimizer_define_operator, name, arity)

    def set_cost(self, operator, cost):
        if operator is None:
            self._call(limestone_optimizer_set_literal_cost, cost)
        else:
            self._call(limestone_optimizer_set_operator_cost, operator, cost)

    def set_limits(self, iterations=20, nodes=10000, classes=10000,
                   reconstructed_nodes=10000, time_ms=0, trace=True):
        limits = limestone_optimization_limits()
        limits.iterations, limits.nodes, limits.classes = iterations, nodes, classes
        limits.reconstructed_nodes, limits.time_ms, limits.trace = reconstructed_nodes, time_ms, trace
        self._call(limestone_optimizer_set_limits, limits)

    def define_graph_operator(self, opcode, term_operator, type, commutative=False):
        self._call(limestone_optimizer_define_graph_operator, opcode, term_operator, type, commutative)

    def attach(self, target):
        """Copy configuration into a raw target handle, independent of this owner."""
        error = limestone_error()
        if limestone_target_set_optimizer(target, self._pointer(), error) != LIMESTONE_OK:
            raise LimestoneError(error)

    @property
    def rule_count(self):
        return limestone_optimizer_rule_count(self._pointer())

    def saturate(self, expression, source_name=None):
        return Optimization(_checked_create(limestone_optimizer_saturate,
                            self._pointer(), expression, source_name))

    def binary_transform(self, context_identity, legality, userdata=None, release=None):
        """Copy a semantic optimizer using a native extension's C proof callback."""
        return BinaryTransform(_checked_create(limestone_optimizer_binary_transform,
                               self._pointer(), context_identity, legality, userdata, release))


class BinaryTransform(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_binary_transform_destroy)

    @property
    def identity(self):
        return limestone_binary_transform_identity(self._pointer())

    @property
    def cacheable(self):
        return bool(limestone_binary_transform_is_cacheable(self._pointer()))


class TranslatedRegion(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_translated_region_destroy)

    @property
    def valid(self):
        return bool(limestone_translated_region_is_valid(self._pointer()))

    @property
    def compiled(self):
        return bool(limestone_translated_region_is_compiled(self._pointer()))

    @property
    def bytes(self):
        return limestone_translated_region_bytes_copy(self._pointer(), False)

    @property
    def guest_bytes(self):
        return limestone_translated_region_bytes_copy(self._pointer(), True)


class BinaryRuntime(_Handle):
    """Owning translation/heat runtime. Native installers use the C callback ABI."""
    def __init__(self, source, target=None, hot_threshold=10, max_regions=1024, transform=None):
        options = limestone_runtime_options()
        options.hot_threshold, options.max_regions = hot_threshold, max_regions
        super().__init__(_checked_create(limestone_runtime_create_with_transform, source._pointer(),
                         source._pointer() if target is None else target._pointer(),
                          options, None if transform is None else transform._pointer(),
                          None, None), limestone_runtime_destroy)

    def prepare(self, data, guest_address, target_address=0):
        data = bytes(data)
        array = ByteArray(len(data))
        for index, value in enumerate(data):
            array[index] = value
        return TranslatedRegion(_checked_create(limestone_runtime_prepare, self._pointer(),
                                array.cast(), len(data), guest_address, target_address))

    @property
    def resident_count(self):
        return limestone_runtime_resident_count(self._pointer())

    def invoke(self, region):
        error = limestone_error()
        status, value = limestone_runtime_invoke(self._pointer(), region._pointer(), error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return value

    def invalidate(self, guest_address, size):
        error = limestone_error()
        status, count = limestone_runtime_invalidate(self._pointer(), guest_address, size, error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return count


class ObjectTarget(_Handle):
    """Owning ELF identity/relocation metadata; bytes are installed by the host."""
    def __init__(self, isa):
        super().__init__(_checked_create(limestone_object_target_load_isa, isa), limestone_object_target_destroy)

    def create(self):
        return ObjectFile(handle=_checked_create(limestone_object_create, self._pointer()))

    def from_code(self, data, symbol):
        return ObjectFile(handle=_checked_create(limestone_object_from_code, self._pointer(), bytes(data), symbol))

    def from_module(self, module, symbol):
        return ObjectFile(handle=_checked_create(limestone_module_object, module._pointer(), self._pointer(), symbol))

    def link(self, objects, base_address=0, max_size=64*1024*1024, externals=None, archives=()):
        handles = [object._pointer() for object in objects]
        libraries = [archive._pointer() for archive in archives]
        return LinkedImage(_checked_create(limestone_archive_link, self._pointer(), handles, libraries, base_address,
                           max_size, {} if externals is None else externals))


class ObjectArchive(_Handle):
    """Owning ar archive with independent member snapshots."""
    def __init__(self, data=None, source_name=None):
        handle = (_checked_create(limestone_archive_create) if data is None else
                  _checked_create(limestone_archive_load, bytes(data), source_name))
        super().__init__(handle, limestone_archive_destroy)

    def add(self, name, object):
        error = limestone_error()
        if limestone_archive_add_member(self._pointer(), name, object._pointer(), error) != LIMESTONE_OK:
            raise LimestoneError(error)

    @property
    def names(self):
        return [limestone_archive_member_name(self._pointer(), index)
                for index in range(limestone_archive_member_count(self._pointer()))]

    def member(self, index):
        return ObjectFile(handle=_checked_create(limestone_archive_get_member, self._pointer(), index))

    def emit(self):
        handle = _checked_create(limestone_archive_emit, self._pointer())
        try:
            return limestone_object_data_bytes_copy(handle)
        finally:
            limestone_object_data_destroy(handle)


class ObjectFile(_Handle):
    def __init__(self, data=None, source_name=None, handle=None):
        if handle is None:
            handle = _checked_create(limestone_object_load_elf, bytes(data), source_name)
        super().__init__(handle, limestone_object_destroy)

    @property
    def text(self):
        return limestone_object_text(self._pointer())

    @property
    def elf_class(self):
        return limestone_object_elf_class(self._pointer())

    def emit(self):
        handle = _checked_create(limestone_object_emit_elf, self._pointer())
        try:
            return limestone_object_data_bytes_copy(handle)
        finally:
            limestone_object_data_destroy(handle)

    def add_section(self, name, data=b"", flags=0, alignment=1, zero_fill=0):
        error = limestone_error()
        status, id = limestone_python_object_add_section(self._pointer(), name, LIMESTONE_OBJECT_NOBITS if zero_fill else LIMESTONE_OBJECT_PROGBITS,
                     flags, alignment, bytes(data), zero_fill, error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return id

    def add_symbol(self, name, section=LIMESTONE_OBJECT_UNDEFINED, value=0, size=0,
                   binding=LIMESTONE_SYMBOL_GLOBAL, type=0, visibility=0):
        error = limestone_error()
        status, id = limestone_python_object_add_symbol(self._pointer(), name, section, value, size, binding, type, visibility, error)
        if status != LIMESTONE_OK:
            raise LimestoneError(error)
        return id

    def add_relocation(self, section, symbol, type, offset, addend=0, *, implicit_addend=False):
        relocation = limestone_object_relocation()
        relocation.section, relocation.symbol, relocation.type = section, symbol, type
        relocation.offset, relocation.addend = offset, addend
        error = limestone_error()
        if limestone_object_add_relocation_ex(self._pointer(), relocation, int(implicit_addend), error) != LIMESTONE_OK:
            raise LimestoneError(error)

    @property
    def relocations(self):
        result = []
        for index in range(limestone_object_relocation_count(self._pointer())):
            relocation, error = limestone_object_relocation(), limestone_error()
            status, implicit = limestone_object_get_relocation_ex(self._pointer(), index, relocation, error)
            if status != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append(dict(section=relocation.section, symbol=relocation.symbol, type=relocation.type,
                          offset=relocation.offset, addend=relocation.addend, implicit_addend=bool(implicit)))
        return result

    @property
    def sections(self):
        result = []
        for index in range(limestone_object_section_count(self._pointer())):
            section, error = limestone_object_section(), limestone_error()
            if limestone_object_get_section(self._pointer(), index, section, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append(dict(name=section.name, type=section.type, flags=section.flags, alignment=section.alignment,
                          data=limestone_object_section_bytes_copy(self._pointer(), index), zero_fill=section.zero_fill))
        return result

    @property
    def symbols(self):
        result = []
        for index in range(limestone_object_symbol_count(self._pointer())):
            symbol, error = limestone_object_symbol(), limestone_error()
            if limestone_object_get_symbol(self._pointer(), index, symbol, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            result.append(dict(name=symbol.name, section=symbol.section, value=symbol.value, size=symbol.size,
                          binding=symbol.binding, type=symbol.type, visibility=symbol.visibility))
        return result


class LinkedImage(_Handle):
    def __init__(self, handle):
        super().__init__(handle, limestone_linked_image_destroy)

    @property
    def bytes(self):
        return limestone_linked_image_bytes_copy(self._pointer())

    @property
    def base_address(self):
        return limestone_linked_image_base(self._pointer())

    @property
    def symbols(self):
        result = {}
        for index in range(limestone_linked_image_symbol_count(self._pointer())):
            symbol, error = limestone_external_symbol(), limestone_error()
            if limestone_linked_image_get_symbol(self._pointer(), index, symbol, error) != LIMESTONE_OK:
                raise LimestoneError(error)
            result[symbol.name] = symbol.address
        return result
%}
