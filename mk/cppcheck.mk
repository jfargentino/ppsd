MISRA_ADDON=tools/misra.json

#CPPCHECK_FLAGS=--enable=warning,style,performance,portability,information
CPPCHECK_FLAGS=--enable=warning,style,performance,portability
CPPCHECK_FLAGS+=--force --inconclusive --std=c99
#CPPCHECK_FLAGS+=--verbose
CPPCHECK_FLAGS+=--quiet

CPPCHECK_FLAGS+=--template='{file}({line});{severity};{id};{message}'

