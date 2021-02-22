# Big Clock

A large seven-segment display clock.

This repository is primarily for the firmware but hardware particulars will be linked in later as they are ready.

## Overview

The Big Clock joins many other large-format seven-segment clocks driven by addressable LEDs. This implementation attempts to scratch the particular itches I had when looking at other folk's implementations that didn't quite do what I wanted. The first part is leveraging the wonderful work done by [John Winans 7SegLED][7seg] hardware. This project has the exact style of LED that I was looking for and even has a tidy algorithm for handling multiple LED strips per digit segment with clean wiring. The second part was to write a custom clock frontend with timezones and NTP, as John's project was built as a counting timer.

The clock frontend had to fulfill a few key features:

- No hardcoded credentials for wifi, implying a captive portal for setup
- Time zone selection and proper handling of daylight savings time
- NTP support for automatically setting the clock
- Persistence across reboots using built-in flash
- Easy to setup and use

With those requirements, I also preferred to rely on libraries from the community instead of reinventing the wheel. There are a number of absolutely invaluable projects leveraged here, without which this project would probably have ended up in the perpetually incomplete bin gathering dust. Namely, [Jack Christensen's Timezone library][timezone] and [Hieromon Ikasamo's AutoConnect library][autoconnect], each of which provided a lot of very fiddly work I have had to manually do in the past but better and more well supported.

[7seg]: https://github.com/johnwinans/7SegLED
[timezone]: https://github.com/JChristensen/Timezone
[autoconnect]: https://github.com/Hieromon/AutoConnect

## Signed OTA Updates

First generate a key pair:

```bash
openssl genrsa -out private.key 2048
openssl rsa -in private.key -outform PEM -pubout \
  | sed -E ':a;N;$!ba;s/\r{0,1}\n/\\n/g' \
  | tr -d '\n' \
  | pbcopy
```

Now take the public key that is in the clipboard and replace the value of `OTA_PUBKEY` in `main.h`. Any new key pair will have to be flashed to the ESP8266 via serial first and manually reset once, then it can be uploaded OTA any number of times after. The code will automatically sign the outgoing packages, they will be verified on-device and applied if the code sign matches.

## Enhancements

While the current state of the project is sufficient to call this "done", there's always more that I'd like to do. Here's the current list which should also server as a reminder if I come back to this some time in the future looking for something to do.

- [ ] Add a button to manually enter captive portal
- [ ] Automatic dimming of LED brightness based on time
- [ ] Update config page to handle multiple values and a generic config object
- [ ] Config to allow specifying LED dimming time and brightness levels
- [ ] Config to allow spefifying NTP server
- [ ] Config to allow setting colors
- [ ] gCal integration? Flash n minutes before your next meeting
