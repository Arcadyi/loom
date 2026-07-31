# Validates a freshly generated respin.json against the schema respin installs.
# Without this the schema is a promise: nothing ever checked that the manifest
# writer and the published schema agree, so they could drift silently.
#
# Skips rather than fails when no JSON Schema validator is available, because
# requiring one would make the whole suite depend on a Python package.

if(NOT RESPIN_EXE OR NOT TEST_DIR OR NOT SCHEMA)
    message(FATAL_ERROR "RESPIN_EXE, TEST_DIR and SCHEMA are required")
endif()

find_program(PYTHON_EXE NAMES python3 python)
if(NOT PYTHON_EXE)
    message(STATUS "No python3 found; skipping schema validation")
    return()
endif()

execute_process(
    COMMAND "${PYTHON_EXE}" -c "import jsonschema"
    RESULT_VARIABLE has_jsonschema
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT has_jsonschema EQUAL 0)
    message(STATUS "python jsonschema is not installed; skipping schema validation")
    return()
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
execute_process(
    COMMAND "${RESPIN_EXE}" new SchemaProbe --org com.example --directory "${TEST_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "respin new failed:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         schema=json.load(open(sys.argv[1])); \
         doc=json.load(open(sys.argv[2])); \
         jsonschema.validate(doc, schema); \
         print('valid')"
        "${SCHEMA}" "${TEST_DIR}/respin.json"
    RESULT_VARIABLE validation
    OUTPUT_VARIABLE validation_output
    ERROR_VARIABLE validation_error
)
if(NOT validation EQUAL 0)
    message(FATAL_ERROR
        "A freshly generated respin.json does not satisfy the shipped schema:\n"
        "${validation_error}")
endif()

# The other direction: the new project.defaultApplication field must be
# accepted, since additionalProperties is false and the schema and the loader
# have to move together. Built by parsing the JSON rather than by text
# substitution -- the project name and the application name are the same string,
# so a plain replace also edits the application object, where the field really
# is invalid.
execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         doc=json.load(open(sys.argv[2])); \
         doc['project']['defaultApplication']=next(iter(doc['applications'])); \
         jsonschema.validate(doc, json.load(open(sys.argv[1])))"
        "${SCHEMA}" "${TEST_DIR}/respin.json"
    RESULT_VARIABLE default_validation
    ERROR_VARIABLE default_error
)
if(NOT default_validation EQUAL 0)
    message(FATAL_ERROR
        "The schema rejects project.defaultApplication, which the loader accepts:\n"
        "${default_error}")
endif()

# And an unknown key must still be refused, or the check above proves nothing.
execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         doc=json.load(open(sys.argv[2])); \
         doc['project']['notAThing']=True; \
         jsonschema.validate(doc, json.load(open(sys.argv[1])))"
        "${SCHEMA}" "${TEST_DIR}/respin.json"
    RESULT_VARIABLE unknown_validation
    ERROR_QUIET
)
if(unknown_validation EQUAL 0)
    message(FATAL_ERROR "The schema accepted an unknown project key")
endif()
