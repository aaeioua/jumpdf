conf = configuration_data()
conf.set('app_name', app_name)
conf.set('app_version', app_version)
conf.set('app_id', app_id)
conf.set('developer_id', developer_id)
conf.set('datadir', get_option('prefix') / get_option('datadir'))

desktop_conf = configure_file(
    input: 'app.desktop.in',
    output: '@0@.desktop.in'.format(app_id),
    configuration: conf
)

desktop_file = i18n.merge_file(
    input: desktop_conf,
    output: '@0@.desktop'.format(app_id),
    type: 'desktop',
    po_dir: '../po',
    install: true,
    install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
    test('Validate desktop file', desktop_utils,
        args: [desktop_file]
    )
endif

appstream_conf = configure_file(
    input: 'app.metainfo.xml.in',
    output: '@0@.metainfo.xml.in'.format(app_id),
    configuration: conf
)

appstream_file = i18n.merge_file(
    input: appstream_conf,
    output: '@0@.metainfo.xml'.format(app_id),
    po_dir: '../po',
    install: true,
    install_dir: get_option('datadir') / 'metainfo'
)

appstream_util = find_program('appstream-util', required: false)
if appstream_util.found()
    test('Validate appstream file', appstream_util,
        args: ['validate', '--nonet', appstream_file]
    )
endif

fs.copyfile('app.png', '@0@.png'.format(app_id),
    install : true,
    install_dir : join_paths(get_option('datadir'), 'icons', 'hicolor', '512x512', 'apps')
)

man_file = configure_file(
    input: 'app.1.in',
    output: '@0@.1'.format(app_name),
    configuration: conf
)

install_man(man_file)

install_data('config.toml',
    install_dir : get_option('datadir') / app_name
)