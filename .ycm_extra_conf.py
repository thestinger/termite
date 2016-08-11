import os
import ycm_core
import subprocess
from clang_helpers import PrepareClangFlags

database = None

def pkg_config(pkg):
  def not_whitespace(string):
    return not (string == '' or string == '\n')
  output = subprocess.check_output(['pkg-config', '--cflags', pkg], universal_newlines=True).strip()
  return filter(not_whitespace, output.split(' '))

flags = [
  '-Wall',
  '-Wextra',
  '-Werror',
  '-pedantic',
  '-Winit-self',
  '-Wshadow',
  '-Wformat=2',
  '-Wmissing-declarations',
  '-Wstrict-overflow=5',
  '-Wcast-align',
  '-Wcast-qual',
  '-Wconversion',
  '-Wunused-macros',
  '-Wwrite-strings',
  '-Wimplicit-fallthrough',
  '-DNDEBUG',
  '-DUSE_CLANG_COMPLETER',
  '-DTERMITE_VERSION="ycm"',
  '-D_POSIX_C_SOURCE=200809L',
  '-std=c++11',
  '-x',
  'c++'
]

flags += pkg_config('gtk+-3.0')
flags += pkg_config('vte-2.91')


def DirectoryOfThisScript():
  return os.path.dirname( os.path.abspath( __file__ ) )


def MakeRelativePathsInFlagsAbsolute( flags, working_directory ):
  if not working_directory:
    return flags
  new_flags = []
  make_next_absolute = False
  path_flags = [ '-isystem', '-I', '-iquote', '--sysroot=' ]
  for flag in flags:
    new_flag = flag

    if make_next_absolute:
      make_next_absolute = False
      if not flag.startswith( '/' ):
        new_flag = os.path.join( working_directory, flag )

    for path_flag in path_flags:
      if flag == path_flag:
        make_next_absolute = True
        break

      if flag.startswith( path_flag ):
        path = flag[ len( path_flag ): ]
        new_flag = path_flag + os.path.join( working_directory, path )
        break

    if new_flag:
      new_flags.append( new_flag )
  return new_flags


def FlagsForFile( filename ):
  if database:
    # Bear in mind that compilation_info.compiler_flags_ does NOT return a
    # python list, but a "list-like" StringVec object
    compilation_info = database.GetCompilationInfoForFile( filename )
    final_flags = PrepareClangFlags(
        MakeRelativePathsInFlagsAbsolute(
            compilation_info.compiler_flags_,
            compilation_info.compiler_working_dir_ ),
        filename )

    # NOTE: This is just for YouCompleteMe; it's highly likely that your project
    # does NOT need to remove the stdlib flag. DO NOT USE THIS IN YOUR
    # ycm_extra_conf IF YOU'RE NOT 100% YOU NEED IT.
    try:
      final_flags.remove( '-stdlib=libc++' )
    except ValueError:
      pass
  else:
    relative_to = DirectoryOfThisScript()
    final_flags = MakeRelativePathsInFlagsAbsolute( flags, relative_to )

  return {
    'flags': final_flags,
    'do_cache': True
  }
