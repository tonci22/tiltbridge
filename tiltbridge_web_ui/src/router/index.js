import { createWebHistory, createRouter } from "vue-router";
import TiltList from "@/components/TiltList.vue";

// NOTE - The below paths MUST also be set up in the relevant firmware!
const routes = [
    {
        path: "/",
        name: "Home",
        component: TiltList,
    },
    {
        path: "/config/",
        name: "ConfigView",
        component: () => import('@/components/ConfigPage.vue'),
        redirect: {"name": "TiltBridgeConfig"},
        children: [
            {
                path: 'tiltbridge/',
                component: () => import('@/components/config/TiltBridge/DeviceConfig.vue'),
                name: 'TiltBridgeConfig',
            },
            {
                path: 'queue/',
                component: () => import('@/components/config/TiltBridge/QueueConfig.vue'),
                name: 'QueueConfig',
            },

            // {
            //     // UserPosts will be rendered inside User's <router-view>
            //     // when /user/:id/posts is matched
            //     path: 'posts',
            //     component: UserPosts,
            // },
        ],
    },
    {
        path: "/target/",
        name: "CloudConfigView",
        component: () => import('@/components/CloudConfigPage.vue'),
        redirect: {"name": "FermentrackConfig"},
        children: [
            {
                path: 'legacy_fermentrack/',
                component: () => import('@/components/config/Targets/Fermentrack.vue'),
                name: 'LegacyFermentrackConfig',
            },
            {
                path: 'fermentrack/',
                component: () => import('@/components/config/Targets/Fermentrack2.vue'),
                name: 'FermentrackConfig',
            },
            {
                path: 'gsheets/',
                component: () => import('@/components/config/Targets/GoogleSheets.vue'),
                name: 'GoogleSheetsConfig',
            },
            {
                path: 'brewersfriend/',
                component: () => import('@/components/config/Targets/BrewersFriend.vue'),
                name: 'BrewersFriendConfig',
            },
            {
                path: 'brewfather/',
                component: () => import('@/components/config/Targets/Brewfather.vue'),
                name: 'BrewfatherConfig',
            },
            {
                path: 'grainfather/',
                component: () => import('@/components/config/Targets/Grainfather.vue'),
                name: 'GrainfatherConfig',
            },
            {
                path: 'brewstatus/',
                component: () => import('@/components/config/Targets/Brewstatus.vue'),
                name: 'BrewstatusConfig',
            },
            {
                path: 'taplistio/',
                component: () => import('@/components/config/Targets/TaplistIO.vue'),
                name: 'TaplistIOConfig',
            },
            {
                path: 'mqtt/',
                component: () => import('@/components/config/Targets/MQTT.vue'),
                name: 'MQTTConfig',
            },
            {
                path: 'influxdb/',
                component: () => import('@/components/config/Targets/InfluxDB.vue'),
                name: 'InfluxDBConfig',
            },
            {
                path: 'generic/',
                component: () => import('@/components/config/Targets/GenericTarget.vue'),
                name: 'GenericTargetConfig',
            },
        ],
    },
    {
        path: "/help/",
        name: "Help",
        component: () => import('@/components/about/HelpPage.vue'),
    },
    {
        path: "/about/",
        name: "About",
        component: () => import('@/components/about/About.vue'),
    },
    {
        // ":color" is really "colour name or canonical device id" - the panel forwards it to
        // the API as whichever one it looks like, so colour-only bookmarks keep working.
        path: "/calibrate/:color/",
        name: "Calibration",
        component: () => import('@/components/calibration/CalibrationPanel.vue'),
        props: true
    },
];

const router = createRouter({
    history: createWebHistory(),
    routes,
});

export default router;