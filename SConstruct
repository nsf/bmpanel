# vim:ft=python:
#==============================================================================
# BMPanel
#
# Copyright (c) 2008 nsf
#==============================================================================

import sys
import os

#------------------------------------------------------------------------------
# command line options
#------------------------------------------------------------------------------

opts = Options('.options.cache')
opts.AddOptions(
	('debug', 'set to 1 if you want debug build', 0),
	('memdebug', 'set to 1 if you want to become memleak hunter', 0),
	('prefix', 'install program in prefix', '/usr'))

env = Environment(options=opts)
opts.Save('.options.cache', env)

Help(opts.GenerateHelpText(env))

#------------------------------------------------------------------------------
# custom PRINT_CMD_LINE_FUNC
#------------------------------------------------------------------------------

varc = 0
varl = 0

def custom_printer(s, target, src, env):
	global varc, varl 
	if not varc:
		print 'Compiler Flags:', env.Dictionary()['CCFLAGS']
		varc = 1
	if not varl:
		print 'Libs:', ', '.join(env.Dictionary()['LIBS'])
		varl = 1

	src_pf_pos = str(src[0]).rfind('.')
	if src_pf_pos == -1:
		src_pf_pos = 0

	src_pf = str(src[0])[src_pf_pos:]

	if src_pf in env['CPPSUFFIXES']:
		print '  C:', ', '.join([str(x) for x in src])
		return

	if src_pf == env['OBJSUFFIX']:
		print '  L:', ', '.join([str(x) for x in target])
		return

	print s

def set_custom_printer():
	env['PRINT_CMD_LINE_FUNC'] = custom_printer

#------------------------------------------------------------------------------
# pkg-config related functions
#------------------------------------------------------------------------------

def append_lib_flags(context, libs):
	context.env.ParseConfig('pkg-config --cflags --libs %s' % libs);

#------------------------------------------------------------------------------
# configuration helpers
#------------------------------------------------------------------------------

def check_pkg_config(context, version):
	context.Message('Checking for pkg-config >= %s... ' % version)
	ret = context.TryAction('pkg-config --atleast-pkgconfig-version=%s' % version)[0]
	context.Result(ret)
	return ret

def check_pkg(context, name):
	context.Message('Checking for %s... ' % name)
	ret = context.TryAction('pkg-config --exists \'%s\'' % name)[0]
	context.Result(ret)
	if ret:
		append_lib_flags(context, name)
	return ret

def create_configure():
	tests = {'check_pkg_config' : check_pkg_config,
		 'check_pkg' : check_pkg}

	conf = env.Configure(custom_tests = tests)
	return conf

def print_not_found_error(what):
	print('%s not found, but you must have it installed to build this software' % what)

def check_c_header_with_error(context, header):
	if not context.CheckCHeader(header):
		print_not_found_error(header)
		Exit(1)

#------------------------------------------------------------------------------
# configure
#------------------------------------------------------------------------------

if not 'install' in COMMAND_LINE_TARGETS:
	conf = create_configure()

	check_c_header_with_error(conf, 'stdio.h')
	check_c_header_with_error(conf, 'stdarg.h')
	check_c_header_with_error(conf, 'stdlib.h')
	check_c_header_with_error(conf, 'string.h')
	check_c_header_with_error(conf, 'signal.h')
	check_c_header_with_error(conf, 'sys/time.h')
	check_c_header_with_error(conf, 'sys/types.h')
	check_c_header_with_error(conf, 'unistd.h')
	check_c_header_with_error(conf, 'time.h')

	if not conf.CheckLibWithHeader('ev', 'ev.h', 'C'):
		print_not_found_error('libev')
		Exit(1)

	if not conf.check_pkg_config('0.20'):
		print_not_found_error('pkg-config')
		Exit(1)

	if not conf.check_pkg('imlib2'):
		print_not_found_error('imlib2')
		Exit(1)

	env = conf.Finish()

#------------------------------------------------------------------------------
# post configure
#------------------------------------------------------------------------------

if not 'install' in COMMAND_LINE_TARGETS:
	set_custom_printer()
	env.Append(CCFLAGS = ' -std=c99 -Wall -DPREFIX=\\\"' + env['prefix'] + '\\\"')

	if int(env['debug']):
		env.Append(CCFLAGS = ' -O0 -g -DLOG_ASSERT_ENABLED -DDEBUG ')
	else:
		env.Append(CCFLAGS = ' -O2 ', LINKFLAGS = ' -s ')

	if int(env['memdebug']):
		env.Append(CCFLAGS = ' -DMEMDEBUG ')

#------------------------------------------------------------------------------
# build & install
#------------------------------------------------------------------------------

if not 'install' in COMMAND_LINE_TARGETS:
	objs = SConscript('src/SConscript', exports='env', build_dir='build/src', duplicate=0)
	build = env.Program(target = 'bmpanel', source = [objs])
	env.Alias('build', build)

install_bin = env.Install(os.path.join(env['prefix'], 'bin'), 'bmpanel')
install_themes = env.Install(os.path.join(env['prefix'], 'share/bmpanel'), 'themes')

env.Alias('install', install_bin)
env.Alias('install', install_themes)
Default('build')
