# Validates a freshly generated loom.json against the schema loom installs.
# Without this the schema is a promise: nothing ever checked that the manifest
# writer and the published schema agree, so they could drift silently.
#
# Skips rather than fails when no JSON Schema validator is available, because
# requiring one would make the whole suite depend on a Python package. The skip
# is a WARNING rather than STATUS, and STRICT turns it into a failure: reported
# as a silent pass, this gate ran nowhere -- including in CI, which did not
# install jsonschema while CONTRIBUTING.md claimed it did.

if(NOT LOOM_EXE OR NOT TEST_DIR OR NOT SCHEMA OR NOT DESIGN_SCHEMA)
    message(FATAL_ERROR
        "LOOM_EXE, TEST_DIR, SCHEMA and DESIGN_SCHEMA are required")
endif()

function(loom_unavailable reason)
    if(STRICT)
        message(FATAL_ERROR "${reason}, and LOOM_STRICT_SCHEMA_TEST is on")
    endif()
    message(WARNING "${reason}; skipping schema validation")
endfunction()

find_program(PYTHON_EXE NAMES python3 python)
if(NOT PYTHON_EXE)
    loom_unavailable("No python3 found")
    return()
endif()

execute_process(
    COMMAND "${PYTHON_EXE}" -c "import jsonschema"
    RESULT_VARIABLE has_jsonschema
    OUTPUT_QUIET ERROR_QUIET
)
if(NOT has_jsonschema EQUAL 0)
    loom_unavailable("python jsonschema is not installed")
    return()
endif()

file(REMOVE_RECURSE "${TEST_DIR}")
execute_process(
    COMMAND "${LOOM_EXE}" new SchemaProbe --org com.example --directory "${TEST_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "loom new failed:\n${output}\n${error}")
endif()

execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         schema=json.load(open(sys.argv[1])); \
         doc=json.load(open(sys.argv[2])); \
         jsonschema.validate(doc, schema); \
         print('valid')"
        "${SCHEMA}" "${TEST_DIR}/loom.json"
    RESULT_VARIABLE validation
    OUTPUT_VARIABLE validation_output
    ERROR_VARIABLE validation_error
)
if(NOT validation EQUAL 0)
    message(FATAL_ERROR
        "A freshly generated loom.json does not satisfy the shipped schema:\n"
        "${validation_error}")
endif()

execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         schema=json.load(open(sys.argv[1])); \
         doc=json.load(open(sys.argv[2])); \
         jsonschema.validate(doc, schema); \
         print('valid')"
        "${DESIGN_SCHEMA}" "${TEST_DIR}/design/tokens.json"
    RESULT_VARIABLE design_validation
    OUTPUT_VARIABLE design_validation_output
    ERROR_VARIABLE design_validation_error
)
if(NOT design_validation EQUAL 0)
    message(FATAL_ERROR
        "A freshly generated design file does not satisfy the shipped schema:\n"
        "${design_validation_error}")
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
        "${SCHEMA}" "${TEST_DIR}/loom.json"
    RESULT_VARIABLE default_validation
    ERROR_VARIABLE default_error
)
if(NOT default_validation EQUAL 0)
    message(FATAL_ERROR
        "The schema rejects project.defaultApplication, which the loader accepts:\n"
        "${default_error}")
endif()

# Declared application states are constrained to the shape a variant prefix can
# take. The scaffolded design file above already covers the accept direction --
# it declares one -- so this is the refusal, without which that proves nothing.
execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         doc=json.load(open(sys.argv[2])); \
         doc['states']={'Not A State': 'wrong shape'}; \
         jsonschema.validate(doc, json.load(open(sys.argv[1])))"
        "${DESIGN_SCHEMA}" "${TEST_DIR}/design/tokens.json"
    RESULT_VARIABLE state_shape_validation
    ERROR_QUIET
)
if(state_shape_validation EQUAL 0)
    message(FATAL_ERROR "The design schema accepted a malformed state name")
endif()

# And an unknown key must still be refused, or the check above proves nothing.
execute_process(
    COMMAND "${PYTHON_EXE}" -c
        "import json,sys,jsonschema; \
         doc=json.load(open(sys.argv[2])); \
         doc['project']['notAThing']=True; \
         jsonschema.validate(doc, json.load(open(sys.argv[1])))"
        "${SCHEMA}" "${TEST_DIR}/loom.json"
    RESULT_VARIABLE unknown_validation
    ERROR_QUIET
)
if(unknown_validation EQUAL 0)
    message(FATAL_ERROR "The schema accepted an unknown project key")
endif()
