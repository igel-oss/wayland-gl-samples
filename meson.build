project('wl-gl-samples', 'c')

dep_scanner = dependency('wayland-scanner', native: true)
prog_scanner = find_program(dep_scanner.get_pkgconfig_variable('wayland_scanner'))
dep_wp = dependency('wayland-protocols')
dir_wp_base = dep_wp.get_variable(pkgconfig: 'pkgdatadir')

xml_path = '@0@/stable/xdg-shell/xdg-shell.xml'.format(dir_wp_base)

xdg_shell_client_protocol_h = custom_target(
	'xdg-shell-client-header',
	input: xml_path,
	output: 'xdg-shell-client-protocol.h',
	command: [prog_scanner, 'client-header', '@INPUT@', '@OUTPUT@' ],
)

xdg_shell_protocol_c = custom_target(
	'xdg-shell-protocol',
	input: xml_path,
	output: 'xdg-shell-protocol.c',
	command: [ prog_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
)

cc = meson.get_compiler('c')
dep_m = cc.find_library('m', required: true)

dep_egl = dependency('egl')
dep_gl = dependency('glesv2')
dep_wayland_egl = dependency('wayland-egl')
dep_wayland = dependency('wayland-client')
dep_wayland_cursor = dependency('wayland-cursor')
dep_cairo = dependency('cairo')

base_sources = [
	xdg_shell_client_protocol_h,
	xdg_shell_protocol_c,
	'shader.c',
	'common.c',
]

base_dep = [
	dep_wayland,
	dep_wayland_cursor,
	dep_wayland_egl,
	dep_egl,
	dep_gl,
	dep_m,
]

samples = [
	{
		'name': 'gl-cube',
		'sources': [base_sources, 'cube.c'],
		'dep': base_dep
	},
	{
		'name': 'gl-bullet',
		'sources': [base_sources, 'bullet.c'],
		'dep': [base_dep, dep_cairo]
	},
	{
		'name': 'gl-tex-cube',
		'sources': [base_sources, 'tex-cube.c'],
		'dep': [base_dep, dep_cairo]
	},
	{
		'name': 'gl-fbo',
		'sources': [base_sources, 'fbo.c'],
		'dep': [base_dep, dep_cairo]
	},
	{
		'name': 'gl-instanced-rendering1',
		'sources': [base_sources, 'instanced_rendering1.c'],
		'dep': [base_dep, dep_cairo]
	},
	{
		'name': 'gl-instanced-rendering2',
		'sources': [base_sources, 'instanced_rendering2.c'],
		'dep': [base_dep, dep_cairo]
	},
	{
		'name': 'gl-instanced-rendering3',
		'sources': [base_sources, 'instanced_rendering3.c'],
		'dep': [base_dep, dep_cairo]
	},
]

foreach s: samples
	executable(
		s.get('name'),
		s.get('sources'),
		dependencies: s.get('dep')
	)
endforeach
