# TiltBridge

[![TiltBridge Logo](http://www.tiltbridge.com/static/img/tiltbridge_logo.png "TiltBridge")](http://www.tiltbridge.com/)

[![Documentation Status](https://readthedocs.org/projects/tiltbridge/badge/?version=master)](http://tiltbridge.readthedocs.io/en/master/?badge=master)
                
#### Bring WiFi to your Tilt Hydrometer

TiltBridge is a single-component solution for bridging the gap between your Tilt Hydrometer's Bluetooth signal and the internet. Eliminate the need for a spare phone/tablet to live next to your fermenter without losing the ability to log your gravity data.

#### Log to the Cloud

TiltBridge automatically logs your data to the following cloud data services:

* [Fermentrack](https://www.fermentrack.com/)
* [BrewPi Remix](https://www.brewpiremix.com)
* [Brewer's Friend](http://www.brewersfriend.com/)
* [Brewfather](https://brewfather.app)
* Google Sheets
* [Brewstatus](https://brewstat.us)
* [Grainfather](https://community.grainfather.com/)
* [Taplist.io](https://taplist.io)
* [MQTT](https://mqtt.org/) Broker
* InfluxDB v2.x

#### Features

* One-glance Gravity Readings
* Gravity Web Dashboard
* Log multiple Tilts at once
* Log to multiple cloud services simultaneously
* Single component build


#### Build a TiltBridge

Building a TiltBridge is simple - the hardest decision in most cases is the enclosure. For more information, read the [documentation](http://docs.tiltbridge.com/).

Once you've acquired your hardware, simply flash the TiltBridge firmware with [BrewFlasher](http://www.brewflasher.com/). Easy!


### Officially Supported Hardware

The **ESP32** firmware has been reorganized into four build types, each of which supports different sets of hardware:

* **Large TFT** - Supports the D32 Pro + D32 LCD Screen and "Cheap Yellow Display" (CYD) ESP32
* **Small TFT** - Supports the M5 Stick C Plus, M5 Stick C Plus 2, and "TTGO" ESP32 /w ESPI Display
* **SSD1306 OLED** - _Unofficial_ - Supports builds that use OLED displays in a number of configurations (e.g. Heltec-style boards)
* **Headless** - Supports ESP32 boards either without a display, or which is not supported by one of the above

**Note** - "Unofficial" builds listed above (currently just the SSD1306 OLED) are _not_ officially tested before release, and may not be supported by future versions of the TiltBridge firmware


#### ESP32-S3 Builds

The **ESP32-S3** firmware is currently _entirely_ unofficial, though official support may come at a future point. There are currently two build types:

* **ESP32-S3 OLED** - Supports ESP32-S3 devices with an OLED display (e.g. Heltec-style boards) - **See note below**
* **ESP32-S3 Small TFT** - Supports the Lilygo T-Display S3 and M5 Stick S3

**OLED Note** - Support for the ESP32-S3 /w OLED is dependent on continued support for the ESP32 /w SSD1306 OLED display. If support for the ESP32 /w OLED display is dropped, support for the ESP32-S3 /w OLED display will be dropped as well.



#### Requirements

TiltBridge requires an ESP32-based controller properly flashed with the TiltBridge firmware.


### Support TiltBridge

Interested in supporting TiltBridge? Buy a [sticker](https://www.tindie.com/products/thorrak/tiltbridge-sticker/) (or a [magnet](https://www.tindie.com/products/thorrak/tiltbridge-magnet/))!



