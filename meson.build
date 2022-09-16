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

sources = [
	xdg_shell_client_protocol_h,
	xdg_shell_protocol_c,
	'shader.c',
	'common.c',
	'cube.c',
]

executable(
	'gl-cube',
	sources,
	dependencies: [
		dep_wayland,
		dep_wayland_cursor,
		dep_wayland_egl,
		dep_egl,
		dep_gl,
		dep_m,
	],
)
