#include <ctime>
#include <ArduinoJson.h>
#include <Ticker.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include "mqtt_client.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <thorlog.h>
#include "url_utils.h"

#include "tilt/tiltScanner.h"
#include "jsonconfig.h"
#include "version.h"
#include "http_server.h"
#include "main.h"  // for printMem()

#include "sendData.h"


dataSendHandler data_sender; // Global data sender

dataSendHandler::dataSendHandler() {}

void dataSendHandler::init()
{
    init_mqtt();

    // Set up timers
    legacyFermentrackTicker.once(12, [](){data_sender.send_legacy_fermentrack = true;});      // Schedule first send to Legacy Fermentrack
    fermentrackTicker.once(10, [](){data_sender.send_fermentrack = true;});      // Schedule first send to Fermentrack
    mqttTicker.once(20, [](){data_sender.send_mqtt = true;});                    // Schedule first send to MQTT
    brewStatusTicker.once(30, [](){data_sender.send_brewStatus = true;});        // Schedule first send to Brew Status
    brewfatherTicker.once(40, [](){data_sender.send_brewfather = true;});        // Schedule first send to Brewfather
    brewersFriendTicker.once(50, [](){data_sender.send_brewersFriend = true;});  // Schedule first send to Brewer's Friend
    userTargetTicker.once(60, [](){data_sender.send_userTarget = true;});        // Schedule first send to User-defined JSON target
    gSheetsTicker.once(70, [](){data_sender.send_gSheets = true;});              // Schedule first send to Google Sheets
    grainfatherTicker.once(80, [](){data_sender.send_grainfather = true;});      // Schedule first send to Grainfather
    taplistioTicker.once(90, [](){data_sender.send_taplistio = true;});          // Schedule first send to Taplist.io
    influxdbTicker.once(100, [](){data_sender.send_influxdb = true;});           // Schedule first send to InfluxDB
}

void dataSendHandler::process()
{
    if (WiFi.status() == WL_CONNECTED) {
        send_to_legacy_fermentrack();
        send_to_fermentrack();
        send_to_bf_and_bf();
        send_to_grainfather();
        send_to_brewstatus();
        send_to_taplistio();
        send_to_google();
        send_to_mqtt();
        send_to_influxdb();
    }
}


bool dataSendHandler::send_to_bf_and_bf()
{
    bool retval = false;
    if (data_sender.send_brewersFriend && !send_lock)
    {
        send_lock = true;
        // Brewer's Friend
        data_sender.send_brewersFriend = false;
        if (strlen(config.brewersFriendKey) > BREWERS_FRIEND_MIN_KEY_LENGTH) {
            Log.verbose("Calling send to Brewer's Friend.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_BREWERS_FRIEND);
            if (retval)
            {
                Log.notice("Completed send to Brewer's Friend.\r\n");
            }
            else
            {
                Log.verbose("Error sending to Brewer's Friend.\r\n");
            }
        }
        brewersFriendTicker.once(BREWERS_FRIEND_DELAY, [](){data_sender.send_brewersFriend = true;}); // Set up subsequent send to Brewer's Friend
        send_lock = false;
    }

    if (data_sender.send_brewfather && !send_lock)
    {
        send_lock = true;
        // Brewfather
        data_sender.send_brewfather = false;
        if (strlen(config.brewfatherKey) > BREWFATHER_MIN_KEY_LENGTH) {
            Log.verbose("Calling send to Brewfather.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_BREWFATHER);
            if (retval)
            {
                Log.notice("Completed send to Brewfather.\r\n");
            }
            else
            {
                Log.verbose("Error sending to Brewfather.\r\n");
            }
        }
        brewfatherTicker.once(BREWFATHER_DELAY, [](){data_sender.send_brewfather = true;}); // Set up subsequent send to Brewfather
        send_lock = false;
    }


    if (data_sender.send_userTarget && !send_lock)
    {
        send_lock = true;
        // User Target
        data_sender.send_userTarget = false;
        if (strlen(config.userTargetURL) > USER_TARGET_MIN_URL_LENGTH)
        {
            Log.verbose("Calling send to User Target.\r\n");
            retval = data_sender.send_to_bf_and_bf(BF_MEANS_USER_TARGET);
            if (retval)
            {
                Log.notice("Completed send to User Target.\r\n");
            }
            else
            {
                Log.verbose("Error sending to User Target.\r\n");
            }
        }
        userTargetTicker.once(USER_TARGET_DELAY, [](){data_sender.send_userTarget = true;}); // Set up subsequent send to User Target
        send_lock = false;
    }
    return retval;
}

bool dataSendHandler::send_to_bf_and_bf(const uint8_t which_bf)
{
    // This function combines the data formatting for both "BF"s - Brewers
    // Friend & Brewfather. Once the data is formatted, it is dispatched
    // to send_to_url to be sent out.

    bool result = true;
    JsonDocument j;
    char url[128];

    // As this function is being used for both Brewer's Friend and Brewfather,
    // let's determine which we want and set up the URL/API key accordingly.
    if (which_bf == BF_MEANS_BREWFATHER)
    {
        if (strlen(config.brewfatherKey) <= BREWFATHER_MIN_KEY_LENGTH)
        {
            Log.verbose("Brewfather key not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, "http://log.brewfather.net/stream?id=");
        strcat(url, config.brewfatherKey);
    }
    else if (which_bf == BF_MEANS_BREWERS_FRIEND)
    {
        if (strlen(config.brewersFriendKey) <= BREWERS_FRIEND_MIN_KEY_LENGTH)
        {
            Log.verbose("Brewer's Friend key not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, "https://log.brewersfriend.com/stream/");
        strcat(url, config.brewersFriendKey);
    }
    else if (which_bf == BF_MEANS_USER_TARGET)
    {
        if (strlen(config.userTargetURL) <= USER_TARGET_MIN_URL_LENGTH)
        {
            Log.verbose("User target URL not populated. Returning.\r\n");
            return false;
        }
        strcpy(url, config.userTargetURL);
    }
    else
    {
        Log.error("Invalid value of which_bf passed to send_to_bf_and_bf.\r\n");
        return false;
    }

    // Loop through each of the tilt colors cached by tilt_scanner, sending
    // data for each of the active tilts
    tilt_scanner.drop_expired_tilts();
    for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
        char gravity[10];
        char temp[6];

        Log.verbose("Tilt loaded with color name: %s\r\n", tilt_color_names[th.m_color]);
        j["name"] = tilt_color_names[th.m_color];
        th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
        j["temp"] = temp;
        j["temp_unit"] = "F";
        th.cal_smooth_gravity_str(gravity, sizeof(gravity));
        j["gravity"] = gravity;
        j["gravity_unit"] = "G";
        j["device_source"] = "TiltBridge";

        char payload_string[BF_SIZE];
        serializeJson(j, payload_string);

        if (!send_to_url(url, payload_string, content_json))
            result = false; // There was an error with the previous send
    }
    return result;
}

bool dataSendHandler::send_to_grainfather()
{
    bool result = true;

    if (send_grainfather && !send_lock)
    {
        // Brew Status
        send_grainfather = false;
        send_lock = true;

        // Loop through each of the tilt colors cached by tilt_scanner, sending
        // data for each of the active tilts
        tilt_scanner.drop_expired_tilts();
        for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
            // If there's no Grainfather URL for this color, just continue
            if (strlen(config.grainfatherURL[th.m_color].link) == 0)
                continue;

            Log.verbose("Calling send to Grainfather.\r\n");
            char gravity[10];
            char temp[6];
            JsonDocument j;
            Log.verbose("Tilt loaded with color name: %s\r\n", tilt_color_names[th.m_color]);
            th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
            j["Temp"] = temp;
            j["Unit"] = "F";
            th.cal_smooth_gravity_str(gravity, sizeof(gravity));
            j["SG"] = gravity;

            char payload_string[GF_SIZE];
            serializeJson(j, payload_string);

            if (!send_to_url(config.grainfatherURL[th.m_color].link, payload_string, content_json))
                result = false; // There was an error with the previous send
        }
        grainfatherTicker.once(GRAINFATHER_DELAY, [](){data_sender.send_grainfather = true;}); // Set up subsequent send to Grainfather
        send_lock = false;
    }
    return result;
}

bool dataSendHandler::send_to_taplistio()
{
    bool result = true;

    // Check if config.taplistioURL is set, and return if it's not
    if (strlen(config.taplistioURL) <= 10) {
        return false;
    }

    // See if it's our time to send.
    if (!send_taplistio) {
        return false;
    } else if (send_lock) {
        Log.verbose("taplist.io: send lock set.\r\n");
        return false;
    }


    // Since we're only using .once timers, we can just detach/recreate every time and be fine
    taplistioTicker.detach();

    // Attempt to send.
    send_taplistio = false;
    send_lock = true;


    tilt_scanner.drop_expired_tilts();

    for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
        JsonDocument j;
        char payload_string[192];
        char gravity[10];
        char temp[6];

        j["Color"] = tilt_color_names[th.m_color];
        th.converted_temp(temp, sizeof(temp), true);  // Always in Fahrenheit
        j["Temp"] = temp;
        th.cal_smooth_gravity_str(gravity, sizeof(gravity));
        j["SG"] = gravity;
        j["temperature_unit"] = "F";
        j["gravity_unit"] = "G";
        
        serializeJson(j, payload_string);

        Log.verbose("taplist.io: Sending %s Tilt to %s\r\n", tilt_color_names[th.m_color], config.taplistioURL);

        result = send_to_url(config.taplistioURL, payload_string, content_json);
    }

    taplistioTicker.once(config.taplistioPushEvery, [](){data_sender.send_taplistio = true;});
    send_lock = false;
    return result;
}


bool dataSendHandler::send_to_brewstatus()
{
    bool result = true;
    const int payload_size = 512;
    char payload[payload_size];

    if (send_brewStatus && ! send_lock)
    {
        // Brew Status
        send_brewStatus = false;
        send_lock = true;
        if (strlen(config.brewstatusURL) > BREWSTATUS_MIN_URL_LENGTH) {
            Log.verbose("Calling send to Brew Status.\r\n");

            // The payload should look like this when sent to Brewstatus:
            // ('Request payload:', 'SG=1.019&Temp=71.0&Color=ORANGE&Timepoint=43984.33630927084&Beer=Beer&Comment=Comment')
            // BrewStatus ignores Beer, so we just set this to Undefined.
            // BrewStatus will record Comment if it set, but just leave it blank.
            // The Timepoint is Google Sheets time, which is fractional days since 12/30/1899
            // Using https://www.timeanddate.com/date/durationresult.html?m1=12&d1=30&y1=1899&m2=1&d2=1&y2=1970 gives
            // us 25,569 days from the start of Google Sheets time to the start of the Unix epoch.
            // BrewStatus wants local time, so we allow the user to specify a time offset.

            // Loop through each of the tilt colors cached by tilt_scanner, sending data for each of the active tilts
            tilt_scanner.drop_expired_tilts();
            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                char gravity[10];
                char temp[6];
                th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit since we don't send units
                snprintf(payload, payload_size, "SG=%s&Temp=%s&Color=%s&Timepoint=%.11f&Beer=Undefined&Comment=",
                        gravity, temp, tilt_color_names[th.m_color], ((double)std::time(0) + (config.TZoffset * 3600.0)) / 86400.0 + 25569.0);
                
                if (send_to_url(config.brewstatusURL, payload, content_x_www_form_urlencoded)) {
                    Log.notice("Completed send to Brew Status.\r\n");
                } else {
                    result = false;
                    Log.verbose("Error sending to Brew Status.\r\n");
                }
            }
        }
        brewStatusTicker.once(config.brewstatusPushEvery, [](){data_sender.send_brewStatus = true;}); // Set up subsequent send to Brew Status
        send_lock = false;
    }
    return result;
}


bool dataSendHandler::send_to_google()
{
    bool result = true;

    if (send_gSheets && !send_lock) {
        // Google Sheets
        send_gSheets = false;
        send_lock = true;

        //tilt_scanner.deinit();
        JsonDocument payload;
        char payload_string[GSHEETS_JSON];
        JsonDocument retval;
        int httpResponseCode;
        int numSent = 0;
#if (ARDUINO_LOG_LEVEL == 6)
        char buff[1024] = "";
#endif

        // The google sheets handler only fires if we have both a Google Scripts URL to post to, and an email address.
        if (strlen(config.scriptsURL) >= GSCRIPTS_MIN_URL_LENGTH && strlen(config.scriptsEmail) >= GSCRIPTS_MIN_EMAIL_LENGTH) {
            Log.verbose("Checking for any pending Google Sheets pushes.\r\n");
//            Log.verbose("Executing on core %i.\r\n", xPortGetCoreID());
            printMem();

            tilt_scanner.drop_expired_tilts();

            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                // Check if there is a google sheet name associated with the specific Tilt
                if (strlen(config.gsheets_config[th.m_color].name) > 0) {
                    char gravity[10];
                    char temp[6];

                    // If there's a sheet name saved, then we should send the data
                    if (numSent == 0)
                        Log.notice("Beginning GSheets check-in.\r\n");
                    payload["Beer"] = config.gsheets_config[th.m_color].name;
                    th.converted_temp(temp, sizeof(temp), true); // Always in Fahrenheit
                    payload["Temp"] = temp;
                    th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                    payload["SG"] = gravity;
                    payload["Color"] = tilt_color_names[th.m_color];
                    payload["Comment"] = "";
                    payload["Email"] = config.scriptsEmail; // The gmail email address associated with the script on google
                    payload["tzOffset"] = config.TZoffset;

                    serializeJson(payload, payload_string);
                    payload.clear();

                    HTTPClient http;
                    WiFiClientSecure secureClient;

                    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // Follow the 301
                    http.setConnectTimeout(6000);                           // Set 6 second timeout
                    http.setTimeout(10000);                                 // Set 10 second timeout
                    http.setReuse(false);
                    secureClient.setInsecure();                             // Ignore SHA fingerprint

                    if (!http.begin(secureClient, config.scriptsURL)) {      // Connect secure
                        Log.error("Unable to create secure connection to %s.\r\n", config.scriptsURL);
                        result = false;
                    } else {
                        // Failed to open a connection
                        Log.verbose("Created secure connection to %s.\r\n", config.scriptsURL);
                        Log.verbose("Sending the following payload to Google Sheets (%s):\r\n\t\t%s\r\n", tilt_color_names[th.m_color], payload_string);

                        http.addHeader("Content-Type", content_json);   // Specify content-type header
                        httpResponseCode = http.POST(payload_string);               // Send the payload

                        if (httpResponseCode == HTTP_CODE_OK) {  // HTTP_CODE_OK = 200
                            // POST success
#if (ARDUINO_LOG_LEVEL == 6)
                            // We need to use a buffer in order to be able to use the response twice
                            strlcpy(buff, http.getString().c_str(), 1024);
                            Log.verbose("HTTP Response: 200\r\nFull Response:\r\n\t%s\r\n", buff);
                            deserializeJson(retval, buff);
//                                deserializeJson(retval, http.getString().c_str());
#else
                            deserializeJson(retval, http.getString().c_str());
#endif

                            if(strcmp(config.gsheets_config[th.m_color].link, retval["doclongurl"].as<const char *>()) != 0) {
                                Log.verbose("Storing new doclongurl: %s.\r\n", retval["doclongurl"].as<const char *>());
                                strlcpy(config.gsheets_config[th.m_color].link, retval["doclongurl"].as<const char *>(), 255);
                                config.save();
                            }
                            retval.clear();
                            numSent++;
                        } else {
                            // Post generated an error (response code != 200)
                            Log.error("Google send to %s Tilt failed (%d): %s. Response:\r\n%s\r\n",
                                tilt_color_names[th.m_color],
                                httpResponseCode,
                                http.errorToString(httpResponseCode).c_str(),
                                http.getString().c_str());
                            result = false;
                        } // Response code != 200
                    } // Good connection
                    http.end();
                    delay(100);  // Give garbage collection time to run
                } // Check we have a sheet name for the color
            }

            Log.notice("Submitted %l sheet%s to Google.\r\n", numSent, (numSent== 1) ? "" : "s");

        }
        gSheetsTicker.once(GSCRIPTS_DELAY, [](){data_sender.send_gSheets = true;}); // Set up subsequent send to Google Sheets

        //tilt_scanner.init();
        send_lock = false;
    }
    return result;
}


bool dataSendHandler::send_to_url(const char *url, const char *dataToSend, const char *contentType, bool checkBody, const char* bodyCheck)
{
    // This handles the generic act of sending data to an endpoint
    bool retVal = false;
    bool result = false;
    bool https = false;

    if (strlen(dataToSend) > 5 && strlen(url) > 8)
    {
        bool validTarget = false;
        ParsedUrl parsedUrl;
        parseUrl(url, &parsedUrl);

        // There is an issue where the built-in HTTP client for some reason won't resolve mDNS addresses. Instead, we'll
        // resolve the address first, and then pass that to the client if needed.
        IPAddress resolvedIP;
        if (isMDNS(parsedUrl.host))
        {
            // Make sure we can resolve the address
            if (resolveHost(parsedUrl.host, resolvedIP) && resolvedIP != INADDR_NONE)
                validTarget = true;
        }
        else if (isValidIP(parsedUrl.host))
            // We were passed an IP Address
            validTarget = true;
        else
        {
            // If it's not mDNS all we care about is that it's http
            // if (strcmp(parsedUrl.scheme, "http") == 0)
                validTarget = true;
        }
        if (validTarget) {
            if (isMDNS(parsedUrl.host))
                // Use the IP address we resolved (necessary for mDNS)
                Log.verbose("Connecting to: %s at %s on port %l\r\n",
                            parsedUrl.host,
                            resolvedIP.toString().c_str(),
                            parsedUrl.port);
            else
                Log.verbose("Connecting to: %s on port %l\r\n",
                            parsedUrl.host,
                            parsedUrl.port);
        }
        if (strcmp(parsedUrl.scheme, "https") == 0)
            https=true;

        if (validTarget) {
            WiFiClientSecure *secureClient;
            secureClient = new WiFiClientSecure();
            {
                // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
                HTTPClient *http;
                http = new HTTPClient();
                secureClient->setInsecure(); // Don't perform certificate validation. This opens up MITM attacks, but I don't have memory otherwise.

                // Determine if the URL is HTTP or HTTPS and initialize HTTPClient
                if (https) {
                    http->begin(*secureClient, url); // HTTPS
                } else {
                    http->begin(url); // HTTP
                }

                // Set headers
                http->addHeader("Content-Type", contentType);
                http->addHeader("Accept", content_json);

                char userAgent[128];
                snprintf(userAgent, sizeof(userAgent), "tiltbridge/%s (branch %s; build %s)", version(), branch(), build());
                http->setUserAgent(userAgent);

                yield();  // Yield before we lock up the radio

                // Send the request
                Log.verbose("Sent data: %s\r\n", dataToSend);
                int httpResponseCode;
                // httpResponseCode = http->sendRequest("POST", dataToSend);
                httpResponseCode = http->POST(dataToSend);

                // Optionally check the response
                if (httpResponseCode > 0) {
                    // HTTP header has been sent and Server response header has been handled
                    Log.verbose("HTTP Response code: %d\r\n", httpResponseCode);

                    if (checkBody) {
                        String response = http->getString();
                        if (response.indexOf(bodyCheck) >= 0) {
                            result = true;
                        } else {
                            Log.error("Body check failed. Body: %s\r\n", response.c_str());
                        }
                    } else {
                        result = (httpResponseCode == HTTP_CODE_OK);
                    }
                } else {
                    Log.error("Error on sending POST: %s\r\n", http->errorToString(httpResponseCode).c_str());
                    Log.error("Connection failed\r\n");
                }

                // Close connection
                http->end();
                delay(100);  // Give garbage collection time to run
                delete http;
            }
            secureClient->stop();
            delete secureClient;

            return result;

        } else {
            Log.error("Invalid target: %s.\r\n", url);
        }
    } else {
        Log.notice("No URL provided, or no data to send.\r\n");
    }
    // If we reached here, the send was unsuccessful
    return false;
}


bool dataSendHandler::send_to_influxdb()
{
    bool result = true;

    if (send_influxdb && !send_lock)
    {
        send_influxdb = false;
        send_lock = true;

        if (strlen(config.influxdbURL) > INFLUXDB_MIN_URL_LENGTH && 
            strlen(config.influxdbToken) > 0 && 
            strlen(config.influxdbOrg) > 0 && 
            strlen(config.influxdbBucket) > 0) {
            
            Log.verbose("Calling send to InfluxDB.\r\n");
            
            // Build the write API URL
            char writeURL[512];
            snprintf(writeURL, sizeof(writeURL), "%s/api/v2/write?org=%s&bucket=%s&precision=s", 
                     config.influxdbURL, config.influxdbOrg, config.influxdbBucket);

            // Build line protocol data
            String lineData = "";
            uint64_t timestamp = std::time(nullptr);
            
            tilt_scanner.drop_expired_tilts();
            for(tiltHydrometer & th : tilt_scanner.m_tilt_devices) {
                char gravity[10];
                char temp[6];
                char battery_str[4];
                
                th.cal_smooth_gravity_str(gravity, sizeof(gravity));
                th.converted_temp(temp, sizeof(temp), false); // Use configured unit
                th.get_weeks_battery(battery_str, sizeof(battery_str));
                
                // InfluxDB line protocol: measurement,tag1=value1 field1=value1,field2=value2 timestamp
                char line[256];
                snprintf(line, sizeof(line), 
                        "tilt,color=%s,device_source=TiltBridge "
                        "gravity=%s,temperature=%s,temp_units=\"%s\",weeks_on_battery=%s\n",
                        tilt_color_names[th.m_color],
                        gravity, temp, config.tempUnit, battery_str);
                
                lineData += line;
            }

            if (lineData.length() > 0) {
                // Send data using HTTP POST with line protocol
                HTTPClient http;
                WiFiClientSecure secureClient;

                // Determine if URL is HTTPS
                bool useHTTPS = strncmp(config.influxdbURL, "https://", 8) == 0;
                
                if (useHTTPS) {
                    secureClient.setInsecure(); // Don't verify certificates
                    http.begin(secureClient, writeURL);
                } else {
                    http.begin(writeURL);
                }

                // Set headers for InfluxDB v2 API
                http.addHeader("Authorization", String("Token ") + config.influxdbToken);
                http.addHeader("Content-Type", "text/plain; charset=utf-8");
                http.addHeader("Accept", "application/json");

                char userAgent[128];
                snprintf(userAgent, sizeof(userAgent), "tiltbridge/%s (branch %s; build %s)", 
                         version(), branch(), build());
                http.setUserAgent(userAgent);

                // Send the data
                int httpResponseCode = http.POST(lineData);

                if (httpResponseCode > 0) {
                    Log.verbose("InfluxDB HTTP Response code: %d\r\n", httpResponseCode);
                    
                    if (httpResponseCode >= 200 && httpResponseCode < 300) {
                        Log.notice("Completed send to InfluxDB.\r\n");
                    } else {
                        Log.error("InfluxDB returned error code %d: %s\r\n", 
                                 httpResponseCode, http.getString().c_str());
                        result = false;
                    }
                } else {
                    Log.error("Error sending to InfluxDB: %s\r\n", 
                             http.errorToString(httpResponseCode).c_str());
                    result = false;
                }

                http.end();
                if (useHTTPS) {
                    secureClient.stop();
                }
            } else {
                Log.verbose("No Tilt data to send to InfluxDB.\r\n");
            }
        }
        
        influxdbTicker.once(config.influxdbPushEvery, [](){data_sender.send_influxdb = true;}); // Set up subsequent send to InfluxDB
        send_lock = false;
    }
    return result;
}

