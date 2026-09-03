import { i18n } from "@/main.js";

/**
 * The cloud targets, in the order they appear in the sidebar.
 *
 * Shared so the sidebar and the mobile target picker on the cloud config page cannot drift
 * apart - on a phone the picker is the only way to switch targets without going back out to
 * the sidebar.
 *
 * A function rather than a constant because i18n does not exist yet when this module is first
 * evaluated: main.js pulls in App.vue (and therefore this) before it calls createI18n().
 */
export function cloudTargets() {
    return [
        { name: 'Fermentrack 2 / Fermentrack.net', route_name: 'FermentrackConfig' },
        { name: i18n.global.t('cloud_config.fermentrack.legacy_fermentrack_menu_item'), route_name: 'LegacyFermentrackConfig' },
        { name: 'Google Sheets', route_name: 'GoogleSheetsConfig' },
        { name: 'Brewers Friend', route_name: 'BrewersFriendConfig' },
        { name: 'Brewfather', route_name: 'BrewfatherConfig' },
        { name: 'Grainfather', route_name: 'GrainfatherConfig' },
        { name: 'Brewstatus', route_name: 'BrewstatusConfig' },
        { name: 'Taplist.io', route_name: 'TaplistIOConfig' },
        { name: 'MQTT', route_name: 'MQTTConfig' },
        { name: 'InfluxDB', route_name: 'InfluxDBConfig' },
        { name: i18n.global.t('cloud_config.generic_target.generic_target_menu_item'), route_name: 'GenericTargetConfig' },
    ];
}
