# Check-in station

An ESP32-S3 touchscreen station for checking equipment in and out. A Bluetooth barcode scanner feeds it scans, students sign on at the 4" screen with their class, name and project, and everything lands in a Google Sheet. A web dashboard (served by Apps Script) shows what's out from anywhere.

## How it fits together

The firmware is seven modules. Each one hides its internals behind a small API, and they only talk to each other through those APIs.

```
barcode scanner ──BLE──> Scanner ──barcodes──> Ui ──> Display ──> touchscreen
                         (core 0)             (core 1)
                                               │ checkIn / checkOut
                                               v
                           flash cache <──> Inventory
                                               │ events    ^ snapshots
                                               v           │
                        Telemetry ──health──> Cloud ───────┘   (core 0)
                                               │ HTTPS
                                               v
              Station script (apps-script/station, personal Gmail, public)
                                  │                     ^
                            Google Sheet                │ HTTPS + dashboard token
                   (Items, Roster, Projects,            │
                    Log, Metrics)                       │
                                     Dashboard script (apps-script/dashboard,
                                     school account, school sign-in only)
                                                        │
                                                 Dashboard page
```

The school's Google Workspace only allows school-only web apps, and the ESP32 can't sign in to Google. So the public part (the station script and the Sheet) runs on a personal Gmail, and the dashboard runs on a school account, where Google makes viewers sign in with a school account.

| Module | Runs on | Job |
| --- | --- | --- |
| `Scanner` | its own task, core 0 | Finds BLE barcode scanners, connects, turns their data into barcode strings, remembers the scanner across reboots |
| `Network` | its own task, core 0 | Owns Wi-Fi: joins the saved network (or `config.h`'s), reconnects when it drops, scans, and joins networks picked on **Settings > WiFi Options**. A new network is saved only once it connects |
| `Cloud` | its own task, core 0 | Waits for Wi-Fi, uploads check-in/out events (queued + retried), pulls roster, projects and items from Sheets, pushes telemetry |
| `Inventory` | called from anywhere | Holds the roster, projects and items, applies check-ins/outs locally right away, hands events to `Cloud`, caches data in flash |
| `Ui` | Arduino loop, core 1 | The screens in `src/ui/`: Sign On, Check In, Check Out, Settings. Reads from the other modules, never touches Bluetooth or Wi-Fi |
| `Display` | Arduino loop, core 1 | ST7796S panel + touch + LVGL. After `begin()`, you just use LVGL |
| `Telemetry` | called from anywhere | Reads chip health: RAM, PSRAM, temperature, Wi-Fi signal, uptime, last reset reason |

## API cheat sheet

```cpp
// Scanner
void Scanner::begin();                                 // start BLE, auto-reconnect to the saved scanner
void Scanner::discover(bool scannersOnly = true);      // search in the background
std::vector<ScannerInfo> Scanner::found();             // last search results, strongest first
uint32_t Scanner::foundRevision();                     // bumps when found() changes
void Scanner::connect(const ScannerInfo& device);      // connect + remember
void Scanner::forget();                                // disconnect + clear the saved scanner
bool Scanner::nextBarcode(std::string& out);           // pop the next scan (non-blocking)
void Scanner::simulateScan(const std::string& code);   // testing without a scanner
ScannerStatus Scanner::status();

// Inventory
void Inventory::begin();                               // load cache (or demo data)
bool Inventory::applySnapshot(const String& json);     // replace data with a Sheets snapshot
std::vector<std::string> Inventory::classes();
std::vector<std::string> Inventory::people(const std::string& className);
std::vector<std::string> Inventory::projects();         // empty if the Sheet has no Projects tab
bool Inventory::findItem(const std::string& barcode, Item& out);
bool Inventory::checkIn(const std::string& barcode, const std::string& cls, const std::string& person,
                        const std::string& project);
bool Inventory::checkOut(const std::string& barcode, const std::string& cls, const std::string& person,
                         const std::string& project);
uint32_t Inventory::revision();                        // bumps on every change

// Network (Wi-Fi)
void Network::begin();                                 // saved network, or config.h's; keeps it connected
void Network::scan();                                  // search in the background
std::vector<WifiNetwork> Network::networks();          // last search, strongest first
uint32_t Network::networksRevision();                  // bumps when networks() changes
void Network::join(const std::string& ssid, const std::string& password);  // saved only if it connects
void Network::forget();                                // back to config.h's network
bool Network::connected();
NetworkStatus Network::status();

// Cloud
void Cloud::begin();                                   // after Network::begin()
void Cloud::sendEvent(const char* action, const std::string& barcode, const std::string& cls,
                      const std::string& person, const std::string& project);  // queued, never blocks
void Cloud::requestSync();
CloudStatus Cloud::status();

// Telemetry
void Telemetry::begin();
SystemMetrics Telemetry::read();
const char* Telemetry::deviceId();

// Display + Ui
bool Display::begin();   void Display::tick();
void Ui::begin();        void Ui::update();
```

The "revision" numbers are how the screen knows when to redraw without polling everything. `Ui::update()` compares `Inventory::revision()`, `Scanner::foundRevision()` and `Network::networksRevision()` with what it last saw and only rebuilds lists when they moved.

## Using the station

1. **Sign On.** Tap your class, pick your name and project, and tap **Log In**. The bar at the top of Check In and Check Out shows who's signed on.
2. **Returning things.** Scan each item on **Check In**. They line up, and **Check In (N)** returns them all.
3. **Borrowing things.** Tick **Check Into Cart** on Check In, then scan each item, or scan them on **Check Out**. The **Items** pill counts the cart, and **Remove** takes one out. **Check Out (N)** borrows everything in it. Items that are already out, or not in the Sheet, can't go in the cart.
4. **Log Out** when you're done. The station also signs out by itself after 3 minutes with no touch or scan (`SESSION_IDLE_MS` in `config.h`), and forgets anything still in the cart. That way the next student's items never land under your name.
5. **Settings** asks for the 6-digit PIN (`SETTINGS_PIN` in `config.h`) every time it's opened. Before anyone has signed on, tap the status icons at the top right to get there.

A scan while nobody is signed on is kept, and shows up on Check In after the next Log In.

## What happens when you check something out

1. Each scan goes into the cart (`Session` in `src/ui/Session.cpp`), which only takes items that are in.
2. **Check Out (N)** calls `Inventory::checkOut(barcode, class, name, project)` for each item, under the signed-on person.
3. `Inventory` marks the item as out immediately and bumps its revision, so the screens update on the next frame even with no Wi-Fi.
4. `Inventory` calls `Cloud::sendEvent("checkout", ...)`, which drops a JSON event on the upload queue and returns instantly.
5. The `Cloud` task posts the event to Apps Script. The event only leaves the queue once Apps Script answers `{"ok":true}`. Network trouble retries every 5 s; setup problems (wrong token, deployment not public) retry every 60 s. **Settings** says which one it is.
6. `doPost` in the station script updates the item's row in **Items** and appends a row to **Log**, with the project in the Project column.
7. Every 5 minutes, once the upload queue is empty, `Cloud` pulls a fresh snapshot from Sheets into `Inventory`. Edits you make directly in the Sheet reach the station that way. If the Sheet now says an item in the cart is out, it leaves the cart.

Each item is its own event and its own row in **Log**, so borrowing 5 items uploads 5 events. Check-in works the same way: returns line up in `Session`, and **Check In (N)** calls `Inventory::checkIn(...)` for each.

## Setup, in order

### 1a. Station script (personal Gmail)
This one holds the data and talks to the ESP32. Its deployment is public, so it never serves the dashboard, and every request needs a token.

1. Signed in to your **personal** Gmail, create a Google Sheet, then open **Extensions → Apps Script**.
2. Paste `apps-script/station/Code.gs` into `Code.gs`.
3. At the top, set `TOKEN` and `DASHBOARD_TOKEN` to two **different** long random strings (letters and numbers only).
4. Select `setup` in the function dropdown and click **Run**. Approve the permissions. This creates the Items, Roster, Projects, Log and Metrics tabs.
5. Fill in **Roster** (one row per person: Class, Name), **Projects** (one project per row; leave it empty if you don't use projects, and Sign On won't ask) and **Items** (Barcode, Name, Status = `In`, and optionally a Description such as "Camcorder, Canon XA11"). Classes named like `2A` show as a big 2 and a small A, grouped by grade, as in the design.
6. **Deploy → New deployment → Web app**. Execute as: *Me*. Who has access: *Anyone*. Copy the URL ending in `/exec`.
7. Open that URL in a private/incognito window. You should see "This link is for the check-in station", with no sign-in prompt.

### 1b. Dashboard script (school account)
This one only serves the dashboard page. Google makes viewers sign in with a school account, and the script fetches the data from the station script.

1. Signed in to your **school** account, go to [script.google.com](https://script.google.com) → **New project**.
2. Paste `apps-script/dashboard/Code.gs` into `Code.gs`. Add an HTML file named exactly `Dashboard` and paste `apps-script/dashboard/Dashboard.html` into it.
3. At the top, set `STATION_URL` to the station script's `/exec` URL and `DASHBOARD_TOKEN` to the same value as in the station script. Replace `STAFF-DOMAIN-HERE` in `DASHBOARD_DOMAINS` with the staff email domain.
4. Select `testConnection` and click **Run**. Approve the permissions. The log should end with `OK: N items, ...`. If it errors, the message says which link in the chain is broken. An error about permission to call `UrlFetchApp` means the school blocks student scripts from fetching outside data; ask before going further.
5. **Deploy → New deployment → Web app**. Execute as: *Me*. Who has access: *Anyone within* your school. Bookmark that URL; it's the dashboard.

Google only shows the dashboard script a viewer's email when they're in the same Google Workspace as your school account. Staff on a domain in a separate Workspace get the sign-in message instead of the dashboard.

After any later edit, go to **Deploy → Manage deployments → ✏️ → Version: New version → Deploy** in the project you changed. Otherwise the old code keeps running.

**Updating from the version without projects?** Paste the new `apps-script/station/Code.gs`, run `setup` again (it adds the Projects tab and the Description and Project columns, and keeps your data), and deploy a new version. Do the same for `Dashboard.html` in the dashboard script to see projects in Recent activity. Old firmware keeps working with the new script, and new firmware with the old one; projects just aren't recorded until both are updated.

Student names and who has what are stored in the personal Gmail's Sheet. Check that's OK with whoever the station is for.

### 2. Firmware
1. Open this folder in VS Code with PlatformIO.
2. Edit `include/config.h`: the default Wi-Fi name and password, the **station script's** `/exec` URL, the same `TOKEN`, your display and touch pins, and a `SETTINGS_PIN` of your own (6 digits; `000000` out of the box). You can switch Wi-Fi later from the screen: **Settings > WiFi Options** lists nearby networks. Tapping a password network asks for its password; an open network needs a second tap to confirm. The station saves a new network only once it connects; if the password is wrong it goes back to the one it was on. A network saved this way takes priority over `config.h` after every reboot, even after re-flashing. **Forget** there returns to the `config.h` network.
3. **Build → Upload → Monitor.** You should see `[display] ready`, then `[cloud] Sheets: OK` and `[cloud] synced roster + items` once Wi-Fi connects. Anything else after `[cloud] Sheets:` is explained in the table below.
4. No scanner yet? Type a barcode into the Serial Monitor and press Enter. It acts exactly like a scan.

### 3. Item images (optional)
Put 120×120 **baseline** JPEGs in `data/items/`, named by barcode (`data/items/012345678905.jpg`), then run PlatformIO's **Upload Filesystem Image**. Progressive JPEGs won't display. Items with no image say "No photo". Scanning a barcode that isn't in the Sheet shows `data/items/unknown_item.jpg` (or "Unknown item" if that file is missing).

### 4. Scanner
1. Put the scanner in **BLE mode**. For the Netum NT-1228BC, scan the "Bluetooth BLE" setup code (and "Clear Pairing Information" if it was paired before).
2. On the station, open **Settings → Bluetooth Options**, tap **Scan**, and tap your scanner in the list.
3. It's saved to flash. After a reboot, or after the scanner sleeps, the station reconnects on its own every 10 s.

HID-mode (keyboard-style) BLE scanners work too; the station detects which kind it's talking to. Bluetooth Classic-only scanners can't connect because the ESP32-S3 has no Classic radio.

## Things to know

| If you see | It usually means | Fix |
| --- | --- | --- |
| Colors look inverted or red/blue are swapped | Panel variant differs | Flip `cfg.invert` or `cfg.rgb_order` in `Display.cpp` |
| Taps land in the wrong place | Touch rotation doesn't match the screen | Change `cfg.offset_rotation` (0 to 3) in `Display.cpp` |
| `PSRAM not found` and the screen stays dark | PSRAM not enabled | Keep `memory_type = qio_opi` in `platformio.ini` |
| Nothing in Serial Monitor | Board uses the native USB port | Uncomment `-DARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` |
| `Sheets: needs public deployment` | The station hit Google's sign-in page. `SCRIPT_URL` is a school-account URL, or the station script isn't deployed to "Anyone" | Use the station script's `/exec` URL from the personal Gmail, deployed to Anyone |
| `Sheets: wrong API token` | `API_TOKEN` in `config.h` doesn't match `TOKEN` in the station script | Match them. If you changed the script, deploy a new version. Queued events wait and upload once it's fixed |
| `Sheets: got a web page (Wi-Fi login?)` | The Wi-Fi wants a browser sign-in (common on guest networks), or the script crashed | Use a network without a login page. Check **Executions** in Apps Script for errors |
| `Sheets: can't connect (HTTP -1)` | No internet, DNS or TLS failure | Check the network has internet and is 2.4 GHz |
| A network in **Settings > WiFi Options** says "needs username" | It's WPA2-Enterprise (username + password), common on school networks | Not supported. Use a network with a single shared password, or a phone hotspot |
| "Couldn't join X: wrong password?" | The network refused the password. The station is back on the network it was using | Tap the network and type the password again. Passwords are case-sensitive |
| A network in **Settings > WiFi Options** says "old security" | It uses WEP or WPA1, which the Wi-Fi driver refuses | Not supported. Switch the router to WPA2, or use another network |
| A network you expect isn't listed | It's 5 GHz only, hidden, or out of range | The ESP32-S3 only sees 2.4 GHz. For a hidden network, put it in `config.h`, flash, then tap **Forget** on **Settings > WiFi Options** (a network picked on screen takes priority over `config.h`) |
| Changed Wi-Fi in `config.h` but nothing changed | A network picked on screen is saved and wins over `config.h` | Tap **Forget** on **Settings > WiFi Options** |
| `Sheets: server error (HTTP 404)` | Wrong URL, or the deployment was archived | Copy the Station URL again from **Manage deployments** |
| `Sheets: rejected last request` | Apps Script refused one event as bad data. It's dropped so the queue keeps moving | The Serial Monitor prints the reason |
| Dashboard says "Couldn't reach the sheet (...)" | The dashboard script can't get data from the station script | Read the message in the parentheses, or run `testConnection()` in the dashboard script |
| Barcodes lose leading zeros in the Sheet | Column formatted as numbers | Run `setup()` again, or format column A of Items as Plain text |
| Scanner never shows up in Bluetooth Options | Scanner in HID Classic or SPP mode | Switch it to BLE mode, or tick "Show all Bluetooth Devices" to look for it |
| Settings asks for a PIN nobody knows | It's `SETTINGS_PIN` in `config.h` (`000000` out of the box) | Change it there and re-flash |
| Project says "No projects in the Sheet" | The Projects tab is missing or empty, or the station script is the older version | Run `setup` in the station script, add projects, deploy a new version, then tap **Force Sync** |
| Sign On has no class chips | The Roster tab is empty, or the station hasn't synced yet | Fill in Roster, then tap the status icons → PIN → **Force Sync** |

## Where to change common things

- **Timing** (sync, telemetry, retry intervals): `include/config.h`
- **Which devices count as scanners**: `looksLikeScanner()` in `Scanner.cpp` (add brand names to the keyword list)
- **Screen layout and wording**: `src/ui/`, one file per screen (`SignOnScreen.cpp`, `CheckInScreen.cpp`, ...). Each one names its Figma frame and notes where it differs from the design. Moving between screens is in `src/Ui.cpp`
- **Colors, fonts and button shapes**: `src/ui/Theme.h` (the Figma colors) and `Theme.cpp`
- **Fonts and icons**: change the lists in `tools/make_fonts.ps1` and `tools/make_images.ps1` and run them. They write `src/ui/fonts/` and `src/ui/images/` from Lexend and the Figma exports in `Design/export/`
- **The design itself**: `Design/Design.fig` (open it in Figma), with a PNG of each screen next to it
- **Settings PIN and auto sign-out time**: `SETTINGS_PIN` and `SESSION_IDLE_MS` in `include/config.h`
- **What gets logged in Sheets**: `recordEvent_()` in `apps-script/station/Code.gs`
- **What the dashboard shows**: `dashboardData_()` in `apps-script/station/Code.gs`
- **Who can open the dashboard**: `DASHBOARD_DOMAINS` in `apps-script/dashboard/Code.gs`
- **Dashboard look**: the CSS variables at the top of `apps-script/dashboard/Dashboard.html`. Set `RIV_URL` there to add a Rive animation

## Trying the screens on a PC

`tools/ui-sim/run.ps1` runs the station's real screens and `Inventory` on Windows, without the board:

```
powershell -ExecutionPolicy Bypass -File tools\ui-sim\run.ps1
```

It needs MinGW (`g++` and `mingw32-make`) and downloads LVGL and ArduinoJson (the versions in `platformio.ini`) the first time. It taps and scans through signing on, the cart, check-out, returns, the PIN, the Settings pages, auto sign-out and Log Out, and prints PASS or FAIL for each step. Screenshots go to `tools/ui-sim/out`; each `compare_*.png` puts a Figma screen next to the station's version. Bluetooth, Wi-Fi and Sheets are faked (`tools/ui-sim/fakes.cpp`).

## Built and checked against

ESP32 Arduino core 2.0.17 (PlatformIO `espressif32@7.1.3`), NimBLE-Arduino 2.5.1, LVGL 9.2.2, LovyanGFX 1.2.31, ArduinoJson 7.4.3, compiled for ESP32-S3 with OPI PSRAM and C++17. All pinned to exact versions in `platformio.ini`. The screens use the Lexend font (SIL Open Font License) and Iconoir icons (MIT), built into the firmware.
