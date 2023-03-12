Import('env')
import os

env.Tool('component')
env.Tool('dwa_install')
env.Tool('dwa_run_test')
env.Tool('dwa_utils')

from dwa_sdk import DWALoadSDKs
DWALoadSDKs(env)

# Suppress depication warning from tbb-2020.
env.AppendUnique(CPPDEFINES='TBB_SUPPRESS_DEPRECATED_MESSAGES')

# Disable OCIO if < v2
disable_ocio = False
ocio_major_version = os.getenv('REZ_OPENCOLORIO_MAJOR_VERSION')
if ocio_major_version is None or int(ocio_major_version) < 2:
    disable_ocio = True

if disable_ocio:
    env.AppendUnique(CPPDEFINES=['DISABLE_OCIO'])

# For Arras, we've made the decision to part with the studio standards and use #pragma once
# instead of include guards. We disable warning 1782 to avoid the related (incorrect) compiler spew.
if 'icc' in env['COMPILER_LABEL'] and '-wd1782' not in env['CXXFLAGS']:
    env['CXXFLAGS'].append('-wd1782') 		# #pragma once is obsolete. Use #ifndef guard instead.
if 'gcc' in env['COMPILER_LABEL']:
    env['CCFLAGS'].append('-Wno-unknown-pragmas')
    env['CXXFLAGS'].append('-Wno-conversion')      # W: Conversion may alter value
    env['CXXFLAGS'].append('-w')

env.DWASConscriptWalk(topdir='#cmd', ignore=[])
env.DWAResolveUndefinedComponents([])
env.DWAFreezeComponents()

# Set default target
env.Default(env.Alias('@install'))
