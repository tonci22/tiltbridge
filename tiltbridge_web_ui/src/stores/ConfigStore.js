import { defineStore } from 'pinia';
import { mande } from 'mande';
import { genCSRFOptions } from './CSRF';
import { ref } from "vue";

export const useConfigStore = defineStore("ConfigStore", () => {

    const mdnsID = ref("");
    const guid = ref("");
    const invertTFT = ref(false);
    const combineTilts = ref(false);
    const cloudEnabled = ref(false);
    const cloudURL = ref("");
    const cloudAppID = ref("");
    const cloudClientKey = ref("");
    const update_spiffs = ref(false);
    // Match the firmware defaults in src/jsonconfig.h, so a page rendered before
    // getConfig() returns does not briefly show the wrong unit or timezone.
    const TZoffset = ref(2);
    const tempUnit = ref("C");
    const smoothFactor = ref(0);
    const gravityUnit = ref("SG");
    const applyCalibration = ref(false);
    const tempCorrect = ref(false);

    // Outbound sender watchdog (see docs/phase1/03-sender-recovery.md)
    const senderRecoveryEnabled = ref(true);
    const wifiRoamEnabled = ref(true);
    const senderStaleRebootSec = ref(75);

    // Google Sheets v2 protocol opt-in
    const gsheetsV2Enabled = ref(false);

    const brewstatusURL = ref("");
    const brewstatusPushEvery = ref(0);
    const taplistioURL = ref("");
    const taplistioPushEvery = ref(0);

    // Google Sheets (Google Scripts)
    // gsheetsPushEvery is how often a reading is UPLOADED to the spreadsheet. It is not the
    // offline queue's snapshot interval (QueueStore.snapshotIntervalSeconds), which controls
    // how often the queue is written to flash and affects no upload schedule.
    const scriptsURL = ref("");
    const scriptsEmail = ref("");
    const gsheetsPushEvery = ref(600);
    
    // Grainfather URLs
    const grainfatherRedURL = ref("");
    const grainfatherGreenURL = ref("");
    const grainfatherBlackURL = ref("");
    const grainfatherPurpleURL = ref("");
    const grainfatherOrangeURL = ref("");
    const grainfatherBlueURL = ref("");
    const grainfatherYellowURL = ref("");
    const grainfatherPinkURL = ref("");


    const brewersFriendKey = ref("");
    const brewersFriendPushEvery = ref(900);
    const brewfatherKey = ref("");
    const brewfatherPushEvery = ref(900);
    const userTargetURL = ref("");
    const userTargetPushEvery = ref(600);
    const grainfatherPushEvery = ref(900);
    const mqttBrokerHost = ref("");
    const mqttBrokerPort = ref(0);
    const mqttUsername = ref("");
    const mqttPassword = ref("");
    const mqttTopic = ref("");
    const mqttPushEvery = ref(0);
    const have_lcd = ref(false);
    const have_led = ref(false);

    // InfluxDB Settings
    const influxdbURL = ref("");
    const influxdbToken = ref("");
    const influxdbOrg = ref("");
    const influxdbBucket = ref("");
    const influxdbPushEvery = ref(900);

    // Fermentrack Settings
    // Legacy Options
    const fermentrackUrl = ref("");
    const fermentrackPushFrequency = ref(30);
    // FT2 Options
    const fermentrackHostname = ref("");
    const fermentrackPort = ref(80);
    const fermentrackUser = ref("");  // Only used during registration
    const fermentrackDeviceID = ref("");
    const fermentrackAPIKey = ref("");
    const fermentrackRegistrationError = ref(8);  // "Not attempted registration"
    const fermentrackPushEvery = ref(300);


    const genericTargetURL = ref("");
    // const genericTargetPushFrequency = ref(30);

    const tiltConfig = ref([]);

    const loaded = ref(false);
    const configError = ref(false);
    const configUpdateError = ref(false);


    async function getConfig() {
        const remote_api = mande("/api/settings/json/", genCSRFOptions());
        const response = await remote_api.get();
        if (response) {
            mdnsID.value = response.mdnsID;
            guid.value = response.guid;
            invertTFT.value = response.invertTFT;
            combineTilts.value = response.combineTilts;
            cloudEnabled.value = response.cloudEnabled;
            cloudURL.value = response.cloudURL;
            cloudAppID.value = response.cloudAppID;
            cloudClientKey.value = response.cloudClientKey;
            update_spiffs.value = response.update_spiffs;
            TZoffset.value = response.TZoffset;
            tempUnit.value = response.tempUnit;
            smoothFactor.value = response.smoothFactor;
            gravityUnit.value = response.gravityUnit || "SG";
            applyCalibration.value = response.applyCalibration;
            tempCorrect.value = response.tempCorrect;
            senderRecoveryEnabled.value = response.senderRecoveryEnabled ?? true;
            wifiRoamEnabled.value = response.wifiRoamEnabled ?? true;
            senderStaleRebootSec.value = response.senderStaleRebootSec ?? 75;
            gsheetsV2Enabled.value = response.gsheetsV2Enabled ?? false;
            fermentrackUrl.value = response.legacyFermentrackURL;
            fermentrackPushFrequency.value = response.legacyFermentrackPushEvery;
            // Fermentrack 2
            fermentrackHostname.value = response.fermentrackHostname;
            fermentrackPort.value = response.fermentrackPort;
            fermentrackUser.value = response.fermentrackUsername;
            fermentrackDeviceID.value = response.fermentrackDeviceID;
            fermentrackAPIKey.value = response.fermentrackAPIKey;
            fermentrackRegistrationError.value = response.fermentrackRegistrationError;
            fermentrackPushEvery.value = response.fermentrackPushEvery || 300;
            // Others
            genericTargetURL.value = response.userTargetURL;
            brewstatusURL.value = response.brewstatusURL;
            brewstatusPushEvery.value = response.brewstatusPushEvery;
            taplistioURL.value = response.taplistioURL;
            taplistioPushEvery.value = response.taplistioPushEvery;
            
            // Google Sheets Values
            scriptsURL.value = response.scriptsURL;
            scriptsEmail.value = response.scriptsEmail;
            gsheetsPushEvery.value = response.gsheetsPushEvery || 600;
            
            // Grainfather URL Values
            grainfatherRedURL.value = response.Red.grainfatherURL;
            grainfatherGreenURL.value = response.Green.grainfatherURL;
            grainfatherBlackURL.value = response.Black.grainfatherURL;
            grainfatherPurpleURL.value = response.Purple.grainfatherURL;
            grainfatherOrangeURL.value = response.Orange.grainfatherURL;
            grainfatherBlueURL.value = response.Blue.grainfatherURL;
            grainfatherYellowURL.value = response.Yellow.grainfatherURL;
            grainfatherPinkURL.value = response.Pink.grainfatherURL;
            grainfatherPushEvery.value = response.grainfatherPushEvery || 900;

            brewersFriendKey.value = response.brewersFriendKey;
            brewersFriendPushEvery.value = response.brewersFriendPushEvery || 900;
            brewfatherKey.value = response.brewfatherKey;
            brewfatherPushEvery.value = response.brewfatherPushEvery || 900;
            userTargetURL.value = response.userTargetURL;
            userTargetPushEvery.value = response.userTargetPushEvery || 600;
            mqttBrokerHost.value = response.mqttBrokerHost;
            mqttBrokerPort.value = response.mqttBrokerPort;
            mqttUsername.value = response.mqttUsername;
            mqttPassword.value = response.mqttPassword;
            mqttTopic.value = response.mqttTopic;
            mqttPushEvery.value = response.mqttPushEvery;
            have_lcd.value = response.have_lcd;
            have_led.value = response.have_led;

            // InfluxDB Settings
            influxdbURL.value = response.influxdbURL || "";
            influxdbToken.value = response.influxdbToken || "";
            influxdbOrg.value = response.influxdbOrg || "";
            influxdbBucket.value = response.influxdbBucket || "";
            influxdbPushEvery.value = response.influxdbPushEvery || 900;

            // We got a response. Parse the list of Tilts (which are sent by color)
            // for (const tiltColorsKey in TiltColors) {
            //     const tiltColor = TiltColors[tiltColorsKey];
            //     if (tiltColor in response) {
            //         const tiltData = response[tiltColor];
            //         const tilt = new TiltDevice(tiltColor, tiltData.temp, tiltData.temp_unit, tiltData.gravity, tiltData.weeks_on_battery, tiltData.sends_battery, tiltData.high_resolution, tiltData.fw_version, tiltData.rssi, tiltData.gsheets_name, tiltData.gsheets_link);
            //         tilts.value.push(tilt);
            //     }
            // }

            loaded.value = true;
            configError.value = false;
        } else {
            // We weren't able to get a response.
            // TODO - Figure out what I want to do here
            await clearConfig();
            configError.value = true;
        }
    }


    async function clearConfig() {
        mdnsID.value = "";
        guid.value = "";
        invertTFT.value = false;
        combineTilts.value = false;
        cloudEnabled.value = false;
        cloudURL.value = "";
        cloudAppID.value = "";
        cloudClientKey.value = "";
        update_spiffs.value = false;
        TZoffset.value = 2;
        tempUnit.value = "C";
        smoothFactor.value = 0;
        gravityUnit.value = "SG";
        applyCalibration.value = false;
        tempCorrect.value = false;
        senderRecoveryEnabled.value = true;
        wifiRoamEnabled.value = true;
        senderStaleRebootSec.value = 75;
        gsheetsV2Enabled.value = false;
        brewstatusURL.value = "";
        brewstatusPushEvery.value = 0;
        taplistioURL.value = "";
        taplistioPushEvery.value = 0;
        scriptsURL.value = "";
        scriptsEmail.value = "";
        gsheetsPushEvery.value = 600;

        grainfatherRedURL.value = "";
        grainfatherGreenURL.value = "";
        grainfatherBlackURL.value = "";
        grainfatherPurpleURL.value = "";
        grainfatherOrangeURL.value = "";
        grainfatherBlueURL.value = "";
        grainfatherYellowURL.value = "";
        grainfatherPinkURL.value = "";
        grainfatherPushEvery.value = 900;

        brewersFriendKey.value = "";
        brewersFriendPushEvery.value = 900;
        brewfatherKey.value = "";
        brewfatherPushEvery.value = 900;
        userTargetURL.value = "";
        userTargetPushEvery.value = 600;
        mqttBrokerHost.value = "";
        mqttBrokerPort.value = 0;
        mqttUsername.value = "";
        mqttPassword.value = "";
        mqttTopic.value = "";
        mqttPushEvery.value = 0;
        have_lcd.value = false;
        have_led.value = false;

        // InfluxDB Settings
        influxdbURL.value = "";
        influxdbToken.value = "";
        influxdbOrg.value = "";
        influxdbBucket.value = "";
        influxdbPushEvery.value = 900;

        // Legacy Fermentrack
        fermentrackUrl.value = "";
        fermentrackPushFrequency.value = 30;

        // Fermentrack 2
        fermentrackHostname.value = "";
        fermentrackPort.value = 80;
        fermentrackUser.value = "";
        fermentrackDeviceID.value = "";
        fermentrackAPIKey.value = "";
        fermentrackRegistrationError.value = 8; // "Not attempted registration" - not sure whether to do 8 or 9 here
        fermentrackPushEvery.value = 300;

        genericTargetURL.value = "";

        loaded.value = false;
        configUpdateError.value = false;
    }

    async function updateTargetConfig(payload, onSuccess) {
        try {
            const remote_api = mande("/api/settings/targets/", genCSRFOptions());
            const response = await remote_api.put(payload);
            if (response && response.status === "ok") {
                onSuccess();
                configUpdateError.value = false;
            } else {
                configUpdateError.value = true;
            }
        } catch (error) {
            configUpdateError.value = true;
        }
    }

    async function updateDeviceConfig(mdnsID, TZoffset, tempUnit, smoothFactor, invertTFT, gravityUnit, combineTilts) {
        try {
            const remote_api = mande("/api/settings/controller/", genCSRFOptions());
            const response = await remote_api.put({
                mdnsID: mdnsID,
                tzOffset: TZoffset,
                tempUnit: tempUnit,
                smoothFactor: smoothFactor,
                invertTFT: invertTFT,
                gravityUnit: gravityUnit,
                combineTilts: combineTilts,
            });
            if (response && response.status && response.status === "ok") {
                await getConfig();
                configUpdateError.value = false;
            } else {
                // await clearConfig();
                configUpdateError.value = true;
            }
        } catch (error) {
            await clearConfig();
            configUpdateError.value = true;
        }
    }


    /**
     * Sender watchdog settings. Same controller endpoint as the general settings, but written
     * from the Offline Queue page because that is where the reliability knobs live together.
     */
    async function updateSenderRecoveryConfig(recoveryEnabled, staleRebootSec, roamEnabled) {
        try {
            const remote_api = mande("/api/settings/controller/", genCSRFOptions());
            const response = await remote_api.put({
                senderRecoveryEnabled: recoveryEnabled,
                senderStaleRebootSec: staleRebootSec,
                wifiRoamEnabled: roamEnabled,
            });
            if (response && response.status === "ok") {
                senderRecoveryEnabled.value = recoveryEnabled;
                senderStaleRebootSec.value = staleRebootSec;
                wifiRoamEnabled.value = roamEnabled;
                configUpdateError.value = false;
                return true;
            }
            configUpdateError.value = true;
            return false;
        } catch (error) {
            configUpdateError.value = true;
            return false;
        }
    }

    async function updateLegacyFermentrackConfig(ft_url, pushFrequency) {
        await updateTargetConfig({
            legacyFermentrackURL: ft_url,
            legacyFermentrackPushEvery: pushFrequency
        }, () => {
            fermentrackUrl.value = ft_url;
            fermentrackPushFrequency.value = pushFrequency;
        });
    }

    async function updateFermentrackConfig(ftHostname, ftPort, ftUser, pushEvery) {
        await updateTargetConfig({
            fermentrackHostname: ftHostname,
            fermentrackPort: ftPort,
            fermentrackUsername: ftUser,
            fermentrackPushEvery: pushEvery
        }, () => {
            fermentrackHostname.value = ftHostname;
            fermentrackPort.value = ftPort;
            fermentrackUser.value = ftUser;
            fermentrackPushEvery.value = pushEvery;
            fermentrackRegistrationError.value = 8; // "Not attempted registration"
        });
    }

    /*
     * Changing only how often readings are uploaded must not re-register the device, so it goes
     * up on its own rather than through updateFermentrackConfig() - the firmware treats a
     * payload carrying the FT2 connection details as a request to register again.
     */
    async function updateFermentrackPushEvery(pushEvery) {
        await updateTargetConfig({
            fermentrackPushEvery: pushEvery
        }, () => {
            fermentrackPushEvery.value = pushEvery;
        });
    }


    async function updateGoogleSheetsConfig(gs_url, gs_email, v2Enabled, pushEvery) {
        await updateTargetConfig({
            scriptsURL: gs_url,
            scriptsEmail: gs_email,
            gsheetsV2Enabled: v2Enabled,
            gsheetsPushEvery: pushEvery,
        }, () => {
            scriptsURL.value = gs_url;
            scriptsEmail.value = gs_email;
            gsheetsV2Enabled.value = v2Enabled;
            gsheetsPushEvery.value = pushEvery;
        });
    }

    async function updateBrewersFriendConfig(brewersFriendApiKey, pushEvery) {
        await updateTargetConfig({
            brewersFriendKey: brewersFriendApiKey,
            brewersFriendPushEvery: pushEvery,
        }, () => {
            brewersFriendKey.value = brewersFriendApiKey;
            brewersFriendPushEvery.value = pushEvery;
        });
    }

    async function updateBrewfatherConfig(brewfather_key, pushEvery) {
        await updateTargetConfig({
            brewfatherKey: brewfather_key,
            brewfatherPushEvery: pushEvery,
        }, () => {
            brewfatherKey.value = brewfather_key;
            brewfatherPushEvery.value = pushEvery;
        });
    }

    async function updateGrainfatherUrls(grainfatherUrls, pushEvery) {
        await updateTargetConfig({
            grainfatherPushEvery: pushEvery,
            grainfatherURL_red: grainfatherUrls.Red,
            grainfatherURL_green: grainfatherUrls.Green,
            grainfatherURL_black: grainfatherUrls.Black,
            grainfatherURL_purple: grainfatherUrls.Purple,
            grainfatherURL_orange: grainfatherUrls.Orange,
            grainfatherURL_blue: grainfatherUrls.Blue,
            grainfatherURL_yellow: grainfatherUrls.Yellow,
            grainfatherURL_pink: grainfatherUrls.Pink,
        }, () => {
            grainfatherRedURL.value = grainfatherUrls.Red;
            grainfatherGreenURL.value = grainfatherUrls.Green;
            grainfatherBlackURL.value = grainfatherUrls.Black;
            grainfatherPurpleURL.value = grainfatherUrls.Purple;
            grainfatherOrangeURL.value = grainfatherUrls.Orange;
            grainfatherBlueURL.value = grainfatherUrls.Blue;
            grainfatherYellowURL.value = grainfatherUrls.Yellow;
            grainfatherPinkURL.value = grainfatherUrls.Pink;
            grainfatherPushEvery.value = pushEvery;
        });
    }

    async function updateBrewstatusConfig(url, pushEvery) {
        await updateTargetConfig({
            brewstatusURL: url,
            brewstatusPushEvery: pushEvery,
        }, () => {
            brewstatusURL.value = url;
            brewstatusPushEvery.value = pushEvery;
        });
    }

    async function updateTaplistIoConfig(url, pushEvery) {
        await updateTargetConfig({
            taplistioURL: url,
            taplistioPushEvery: pushEvery,
        }, () => {
            taplistioURL.value = url;
            taplistioPushEvery.value = pushEvery;
        });
    }

    async function updateMQTTConfig(host, port, username, password, topic, pushEvery) {
        await updateTargetConfig({
            mqttBrokerHost: host,
            mqttBrokerPort: port,
            mqttUsername: username,
            mqttPassword: password,
            mqttTopic: topic,
            mqttPushEvery: pushEvery,
        }, () => {
            mqttBrokerHost.value = host;
            mqttBrokerPort.value = port;
            mqttUsername.value = username;
            mqttPassword.value = password;
            mqttTopic.value = topic;
            mqttPushEvery.value = pushEvery;
        });
    }

    async function updateGenericTargetConfig(target_url, pushEvery) {
        await updateTargetConfig({
            userTargetURL: target_url,
            userTargetPushEvery: pushEvery,
        }, () => {
            genericTargetURL.value = target_url;
            userTargetPushEvery.value = pushEvery;
        });
    }

    async function updateInfluxDBConfig(url, token, org, bucket, pushEvery) {
        await updateTargetConfig({
            influxdbURL: url,
            influxdbToken: token,
            influxdbOrg: org,
            influxdbBucket: bucket,
            influxdbPushEvery: pushEvery,
        }, () => {
            influxdbURL.value = url;
            influxdbToken.value = token;
            influxdbOrg.value = org;
            influxdbBucket.value = bucket;
            influxdbPushEvery.value = pushEvery;
        });
    }

    async function performAction(actionName) {
        try {
            const remote_api = mande("/api/actions/", genCSRFOptions());
            const response = await remote_api.post({ action: actionName });
            return response && response.status === "ok";
        } catch (error) {
            return false;
        }
    }

    async function resetWifi() {
        return performAction("resetWifi");
    }

    async function resetDevice() {
        return performAction("resetDevice");
    }

    return {
        mdnsID,
        guid,
        invertTFT,
        combineTilts,
        cloudEnabled,
        cloudURL,
        cloudAppID,
        cloudClientKey,
        update_spiffs,
        TZoffset,
        tempUnit,
        smoothFactor,
        gravityUnit,
        applyCalibration,
        tempCorrect,
        senderRecoveryEnabled,
        wifiRoamEnabled,
        senderStaleRebootSec,
        gsheetsV2Enabled,
        brewstatusURL,
        brewstatusPushEvery,
        taplistioURL,
        taplistioPushEvery,
        scriptsURL,
        scriptsEmail,
        grainfatherRedURL,
        grainfatherGreenURL,
        grainfatherBlackURL,
        grainfatherPurpleURL,
        grainfatherOrangeURL,
        grainfatherBlueURL,
        grainfatherYellowURL,
        grainfatherPinkURL,
        grainfatherPushEvery,
        gsheetsPushEvery,
        brewersFriendKey,
        brewersFriendPushEvery,
        brewfatherKey,
        brewfatherPushEvery,
        userTargetURL,
        userTargetPushEvery,
        mqttBrokerHost,
        mqttBrokerPort,
        mqttUsername,
        mqttPassword,
        mqttTopic,
        mqttPushEvery,
        have_lcd,
        have_led,
        influxdbURL,
        influxdbToken,
        influxdbOrg,
        influxdbBucket,
        influxdbPushEvery,
        fermentrackUrl,
        fermentrackPushFrequency,
        fermentrackHostname,
        fermentrackPort,
        fermentrackUser,
        fermentrackDeviceID,
        fermentrackAPIKey,
        fermentrackPushEvery,
        fermentrackRegistrationError,
        genericTargetURL,
        tiltConfig,
        loaded,
        configError,
        configUpdateError,

        getConfig,
        clearConfig,
        updateDeviceConfig,
        updateSenderRecoveryConfig,
        updateLegacyFermentrackConfig,
        updateFermentrackConfig,
        updateFermentrackPushEvery,
        updateGoogleSheetsConfig,
        updateBrewersFriendConfig,
        updateBrewfatherConfig,
        updateGrainfatherUrls,
        updateBrewstatusConfig,
        updateTaplistIoConfig,
        updateMQTTConfig,
        updateGenericTargetConfig,
        updateInfluxDBConfig,
        resetWifi,
        resetDevice
    };
});
