> **Note (historical):** this is a planning-phase artifact from when the
> firmware was written in Moddable JavaScript/TypeScript. It's preserved
> as source material for the original product spec. The active firmware
> is now native ESP-IDF C — see [`firmware.md`](./firmware.md) and
> [`bring-up.md`](./bring-up.md) for current state.

Your IoT water flow sensor implementation plan is ready.

Your Markdown file is ready
[file-tag: code-generated-file-0-1777503765747904011]

### Implementation Highlights:

1.  **Architecture:** The plan uses an **ESP32** connected via **RS485** to a **TUF-2000M** ultrasonic module. It utilizes **Moddable SDK** to allow for **JavaScript/TypeScript** development on the microcontroller, aligning with your preferred stack.
2.  **Logic & Storage:** The backend logic is designed for **Node.js** using `better-sqlite3`. It avoids data bloat by only logging "events" (start/end times) and provides a multi-level warning system based on flow duration (e.g., Level 2 at 30 minutes, Level 3 at 60 minutes).
3.  **Web Flashing Tool:** I have included code for a web interface using the **ESP Web Tools** library. This leverages the **WebSerial API**, allowing you to flash compiled `.bin` files directly from a browser (Chrome or Edge) without requiring the user to install local development tools. 

### To use the Web Flasher:
* You will need to compile your JavaScript code into a `.bin` file using the Moddable `mcconfig` tool.
* Place the `index.html`, `manifest.json`, and your `.bin` files on a web server.
* **Important:** WebSerial requires **HTTPS** to function. If you are testing locally, `localhost` will generally work, but any remote access must be over a secure connection.