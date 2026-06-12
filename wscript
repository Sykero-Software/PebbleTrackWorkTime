#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    patch_clay_for_new_platforms(ctx)
    ctx.load('pebble_sdk')


def build(ctx):
    patch_clay_for_new_platforms(ctx)
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        defines = ['SCREENSHOT_FIXTURES'] if os.environ.get('SCREENSHOT_FIXTURES') else []
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app', defines=defines)

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')

    # The Pebble SDK bundler drops unknown appinfo.json fields, including
    # `companionApp`. The Core/Pebble app reads `companionApp.android.apps[].pkg`
    # from the bundled appinfo.json to route watch->phone AppMessages via
    # PebbleKit Android 2 — without it, it falls back to (unsupported) classic
    # PebbleKit and nothing is delivered. So re-inject it into the .pbw here.
    ctx.add_post_fun(inject_companion_app)


def patch_clay_for_new_platforms(ctx):
    """Teach pebble-clay 1.0.4 about the newer `flint`/`gabbro` boards.

    Clay ships its compiled C library (`libpebble-clay.a`) only for
    aplite/basalt/chalk/diorite/emery. Because Clay is consumed as a Pebble
    *package*, waf links that C lib into every target platform, so a build that
    targets flint/gabbro fails ("doesn't support the platform") even though we
    use Clay purely for its JS config page and never call a single Clay C
    function. We make the lib link on the new boards by adding flint/gabbro
    copies of an existing board's `.a` (diorite->flint, chalk->gabbro) and the
    matching include stubs. The bytes are never executed (no Clay C calls), so
    using another board's lib only needs to satisfy the linker.

    This patches the installed package in node_modules so it survives a
    `pebble package install` / npm reinstall (which restores the upstream files).
    It is idempotent and a no-op once the entries exist. dist.zip is the source
    of truth: the build re-extracts it each time, so patching the extracted
    dist/ tree would not stick — we patch the zip.
    """
    import json, zipfile, os
    clay = ctx.path.find_node('node_modules/pebble-clay')
    if clay is None:
        return
    clay_dir = clay.abspath()
    new_platforms = {'flint': 'diorite', 'gabbro': 'chalk'}

    # 1) package.json targetPlatforms (the dependency-resolution gate).
    pj_path = os.path.join(clay_dir, 'package.json')
    pj = json.load(open(pj_path))
    tps = pj.get('pebble', {}).get('targetPlatforms', [])
    if tps and any(p not in tps for p in new_platforms):
        for p in new_platforms:
            if p not in tps:
                tps.append(p)
        json.dump(pj, open(pj_path, 'w'), indent=2)

    # 2) dist.zip binaries + include stubs (what the linker/compiler consume).
    zip_path = os.path.join(clay_dir, 'dist.zip')
    if not os.path.exists(zip_path):
        return
    zin = zipfile.ZipFile(zip_path, 'r')
    data = {n: zin.read(n) for n in zin.namelist()}
    zin.close()
    changed = False
    for new, src in new_platforms.items():
        bin_dst = 'binaries/%s/libpebble-clay.a' % new
        bin_src = 'binaries/%s/libpebble-clay.a' % src
        inc_dst = 'include/pebble-clay/%s/src/resource_ids.auto.h' % new
        inc_src = 'include/pebble-clay/%s/src/resource_ids.auto.h' % src
        if bin_dst not in data and bin_src in data:
            data[bin_dst] = data[bin_src]
            changed = True
        if inc_dst not in data and inc_src in data:
            data[inc_dst] = data[inc_src]
            changed = True
    if changed:
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zout:
            for n, b in data.items():
                zout.writestr(n, b)
        from waflib import Logs
        Logs.pprint('CYAN', 'Patched pebble-clay for flint/gabbro')


def inject_companion_app(ctx):
    import json, zipfile, shutil, glob, os
    pkg_path = ctx.path.find_node('package.json').abspath()
    companion = json.load(open(pkg_path)).get('pebble', {}).get('companionApp')
    if not companion:
        return
    for pbw in glob.glob(os.path.join(out, '*.pbw')):
        zin = zipfile.ZipFile(pbw, 'r')
        if 'appinfo.json' not in zin.namelist():
            zin.close()
            continue
        info = json.loads(zin.read('appinfo.json'))
        if info.get('companionApp') == companion:
            zin.close()
            continue
        info['companionApp'] = companion
        tmp = pbw + '.tmp'
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == 'appinfo.json':
                    data = json.dumps(info).encode()
                zout.writestr(item, data)
        zin.close()
        shutil.move(tmp, pbw)
        from waflib import Logs
        Logs.pprint('CYAN', 'Injected companionApp into %s' % pbw)
