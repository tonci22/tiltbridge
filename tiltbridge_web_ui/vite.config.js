import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import viteCompression from 'vite-plugin-compression';

// eslint-disable-next-line no-undef
const path = require("path");

// let localServer = "http://0.0.0.0:8000/";
const localServer = "http://192.168.5.127/";

/*
 * mklittlefs (and LittleFS itself) cap a filename at 32 characters. Everything that goes into
 * the image has to fit, including the ".gz" that vite-plugin-compression appends. See the
 * comment on entryFileNames below for what happens when it does not.
 */
const FS_NAME_MAX = 32;   // mklittlefs LFS_NAME_MAX
const HASH_LEN = 8;       // width of rollup's [hash]
const GZ_SUFFIX = '.gz';  // appended by vite-plugin-compression

function fsSafeName(name, ext) {
    // budget = 32 - ".gz" - extension - hash - the "-" joining stem and hash
    const budget = FS_NAME_MAX - GZ_SUFFIX.length - ext.length - HASH_LEN - 1;
    const stem = String(name || 'chunk').replace(/\.[^.]*$/, '');
    return `assets/${stem.slice(0, Math.max(budget, 1))}-[hash]${ext}`;
}

// https://vitejs.dev/config/
export default defineConfig({
    plugins: [
        vue(),
        viteCompression({ verbose: false, deleteOriginFile: true })
    ],
    resolve: {
        alias: {
            "@": path.resolve(__dirname, "./src"),
        },
    },
    build: {
        rollupOptions: {
            output: {
                /*
                 * Content-hashed, under assets/, and short enough for mklittlefs.
                 *
                 * Hashing: unhashed names meant every build wrote index.js, HelpPage.js and
                 * the rest over the same URLs. Since the firmware serves them with a max-age,
                 * a browser that had the page open within that window kept OLD chunks under
                 * the NEW build's names after an uploadfs. Any chunk the new build no longer
                 * emitted then 404'd, the lazy route's dynamic import() rejected, and
                 * router-view rendered an empty page with nothing in the console. A hash makes
                 * that impossible: a different build is a different URL.
                 *
                 * assets/ is what lets the firmware tell "hashed, safe to cache forever" from
                 * the unhashed favicons at the root and the esp_wifi_config UI under wifiui/.
                 * The cache-control block in src/idf_static_files.cpp must agree with this path.
                 *
                 * Length: mklittlefs enforces LFS_NAME_MAX = 32 on the filename. One character
                 * over and it prints "unable to open .../ error adding file!", ABANDONS the rest
                 * of that directory, and PlatformIO still reports SUCCESS - so uploadfs ships an
                 * image with files missing and nothing says so. Not theoretical:
                 * SendTargetErrorMsg-<hash>.js.gz came to 33 and took GenericTarget,
                 * GoogleSheets and Grainfather down with it, blanking every cloud target page.
                 * vite-plugin-compression appends ".gz", so that counts toward the 32.
                 *
                 * Truncating the stem to whatever is left makes the limit impossible to exceed
                 * whatever a component is named. tools/build_ui.py re-checks the finished data/
                 * tree, because public/ files bypass this naming entirely.
                 */
                entryFileNames: (info) => fsSafeName(info.name, '.js'),
                chunkFileNames: (info) => fsSafeName(info.name, '.js'),
                assetFileNames: (info) => {
                    const name = info.name || 'asset';
                    const ext = /\.[^.]+$/.exec(name);
                    return fsSafeName(name, ext ? ext[0] : '');
                }
            }
        }
    },
    server: {
        proxy: {
            '^/api/.*': {
                target: localServer,
                changeOrigin: true,
                // rewrite: (path) => path.replace(/^\/fallback/, '')
            },

            // Old Paths
            '^/json/.*': {
                target: localServer,
                changeOrigin: true,
                // rewrite: (path) => path.replace(/^\/fallback/, '')
            },
            '^/settings/json/.*': {
                target: localServer,
                changeOrigin: true,
                // rewrite: (path) => path.replace(/^\/fallback/, '')
            },
            '^/conf/.*.json': {
                target: localServer,
                changeOrigin: true,
                // rewrite: (path) => path.replace(/^\/fallback/, '')
            },
        }
    }
})
