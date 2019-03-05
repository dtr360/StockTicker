
/*  stock_ticker.ino

    Written by Daniel Rencricca <drencricca@gmail.com>.

    This program obtains stock quote data by scraping it from the cnbc.com
    website and displays this information on an LED display.

    Permission to use, copy, modify, and distribute this software for any
    purpose without fee is hereby granted, provided that this entire notice
    is included in all copies of any software which is or includes a copy
    or modification of this software.

    Notes: This project uses the following libaries:

    The Arduino ESP32 libraries:
        https://github.com/wemos/Arduino_ESP32#installation-instructions

    The AdaFruit LED Backbackpack and GFX libraries:
        https://github.com/adafruit/Adafruit-GFX-Library
        https://github.com/adafruit/Adafruit_LED_Backpack

    The Adafruit Backpack library dosnt support the '.' full stop, and I didnt like some of their font
    choices for the number digits, so I have created a modified version:
        https://github.com/darrendignam/Adafruit_LED_Backpack

    If using ESP32 TTGO with 128x64 pixel OLED display and WiFi, do the following:
    Install: CP210x USB to UART Bridge VCP Drivers from,
      https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers
      This should make available a port called /dev/cu.SLAB_USBtoURART
    Use Upload Speed: 921600
    Use Board: ESP32 Dev Module

    Get OLED library: https://github.com/osresearch/esp32-ttgo.  Decompress and copy the
    OLED folder into arduino libraries folder.

    THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
    WARRANTY.  IN PARTICULAR,  THE AUTHOR MAKES NO REPRESENTATION
    OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY OF THIS
    SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
*/

// ESP32 WiFi Libraries
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>

// ESP32 OLED Libraries
#include "SSD1306.h"
#include "OLEDDisplayUi.h"

// Other Libraries
#include <Regexp.h> // Regex (Nick Gammon)
#include "time.h"
#include "math.h"

// Financial markets open at 9:30am and close at 16:30pm. We only need to
// update quotes during this time period.
#define MARKET_OPEN_HR     9
#define MARKET_OPEN_MIN   30
#define MARKET_CLOSE_HR   16
#define MARKET_CLOSE_MIN   0
#define DISP_DATE_CNT     20 // displays date after every 20 quotes
#define USING_SSD1306

#ifdef USING_SSD1306
SSD1306 display(0x3c, 4, 15);
OLEDDisplayUi ui(&display);
#endif

//////////////////////////////////////////////////////////////////////////////
// The stocks array holds the ticker symbols for the securities for which
// to get quote data. Update this list to include whatever stocks you want.
//
String stocks[] = {"MSFT", "AAPL", "FB", "BP", "NFLX"};
//
//////////////////////////////////////////////////////////////////////////////
// Enter the ssids and passwords for the wifi access point(s) to be used.
//
const char* wifiSSID01     = "wifi_ssid_1";
const char* wifiPassword01 = "password_1";
const char* wifiSSID02     = "wifi_ssid_2";
const char* wifiPassword02 = "password_2";
//
//////////////////////////////////////////////////////////////////////////////

int  intervalUpdate  = 120000;  // wait-time to update quote data (milliseconds)
long lastUpdateTime  = -intervalUpdate; // init

const int   displayWidth = 14; // number of characters for display
const char* updatingMsg  = "UPDATING.............  "; // message if quote not avail
const char* spacer       = "  "; // space between quotes when displayed

const byte stocksLen = sizeof(stocks) / sizeof(stocks[0]);
String     stocksQuotes[stocksLen];  // array of strings to hold quote data
bool       haveQuoteData   = false;

WiFiMulti wifiMulti;

TaskHandle_t TaskGetQuotesH;
TaskHandle_t TaskDisplayH;

bool marketOpen() {
  /**
    Checks whether or not the stock market is currently open
    @param none.
    @return true if market is open, else false.
  */
  time_t      now;
  struct tm*  timeinfo;

  time(&now);
  timeinfo = localtime (&now);

  // char buffer [80];   // for testing
  // strftime (buffer, 80, "Day of week: %w  Time: %H:%M ", timeinfo);   // for testing
  // Serial.println(buffer);   // for testing

  tm openTime  = *localtime(&now);
  tm closeTime = *localtime(&now);

  openTime.tm_hour  = MARKET_OPEN_HR;
  openTime.tm_min   = MARKET_OPEN_MIN;
  openTime.tm_sec   = 0;
  closeTime.tm_hour = MARKET_CLOSE_HR;
  closeTime.tm_min  = MARKET_CLOSE_MIN;

  bool marketOpen;

  // check if it is a weekday during market hours
  if (timeinfo->tm_wday > 0 && timeinfo->tm_wday < 7) {
    marketOpen = difftime(now, mktime(&openTime)) > 0 &&
                 difftime(now, mktime(&closeTime)) < 0;
  }
  //if (!marketOpen)
    //Serial.println("\nMarket is closed.");
//    display.clear();
//    display.drawStringMaxWidth(0, 6, 128, "Market is closed.");
//    display.display();
//    delay(2000);

  return marketOpen;
}

void chopDecimalPlaces(char* numStr) {
  /**
     Eliminates all but 2 decimal places in the number contained in the given
     characer string.
     @param num_str - a pointer to a string containing a number that may or may
     not have decimal places.
     @return nothing.
  */
  int i = 0;
  while (i < strlen(numStr)) {
    if (numStr[i] == '.') {
      i += 3; // skip to the 3rd decimal place, if any
      if (i < strlen(numStr)) {
        numStr[i] = '\0'; // terminate the string
        break;
      }
    }
    i += 1;
  }
}


void getQuote(int index) {
  /**
    Retrieves quote data from the cnbc.com website via HTTPS GET request. Data is
    scraped to get stock price and change information.
    @param index the index of the stocks array for the quote to get.
    @return nothing.
  */
  const char* host  = "www.cnbc.com";
  const int port    = 443;  // SSL port
  char url[40]      = ""; // url for quote data
  //int startTime = millis(); // for testing

  snprintf(url, sizeof(url), "%s%s", "/quotes/?symbol=",  stocks[index].c_str());

  //Serial.printf("Requesting URL: %s\n", url);  // for testing

  // Create a secure client that can connect to the specified internet IP address
  WiFiClientSecure    clientS; // using HTTPS connection

  if (!clientS.connect(host, port)) {
    Serial.println("ERROR: Client could not connect to server.");
    return;
  }

  // You may get and error, [E][WiFiClient.cpp:243] setSocketOption(): 1006 : 9
  // even when setting the timeout longer. The error is a bug in WifiClient but
  // does not seem to affect anything. 
  clientS.setTimeout(5000);

  // Make a get request to the website with the stock quote data.
  clientS.print(String("GET ") + url + " HTTP/1.1\r\n" +
                "Host: " + host + "\r\n" +
                "User-Agent: BuildFailureDetectorESP32\r\n" +
                "Connection: close\r\n\r\n");

  // If GET request was successful then the first line of the stream returned
  // should be "HTTP/1.1 200 OK"
  if (!clientS.findUntil("200 OK", "\n")) {
    //Serial.println("ERROR: Invalid stock ticker");
    stocksQuotes[index] = "Invalid Symbol";
    return;
  }

  String quoteData = "";

  // Find in the stream the location of the data we are interested in
  if (clientS.find("\"structured-data\"")) {

    clientS.readStringUntil('\n'); // read rest of the line (we don't need it)

    // Read one line at a time until a </div> tag is found. This is the data that
    // contains the quote data.
    while (clientS.connected()) {

      String line = clientS.readStringUntil('\n');
      line.trim();

      if (line.startsWith("</div>")) { // not interested in lines below this
        break; // we are done
      }
      else {
        quoteData = quoteData + " " + line;
      }
    } // while (clientS.connected())
  }

  if (quoteData == "") {
    Serial.println("ERROR: could not read data from server");
    return;
  }

  // Move the quote data string to a character buffer so we can use regex to find things
  char buf [quoteData.length() + 1];
  quoteData.toCharArray(buf, quoteData.length() + 1);

  char price [16] = ""; // holds stock price
  char change[16] = ""; // holds change in stock price from previous close
  char result;          // result code of regex match attempt

  MatchState ms(buf); // create a match state object

  // Get the price information from the html data
  result = ms.Match ("price[a-z\" ]*=\"", 0);

  if (result == REGEXP_MATCHED) {
    result = ms.Match ("[0-9.]*", ms.MatchStart + ms.MatchLength);
    ms.GetMatch(price);
  }

  // Get the price change information from the html data
  result = ms.Match("priceChange[a-z\" ]*=\"", 0);

  if (result == REGEXP_MATCHED) {
    result = ms.Match ("[0-9.-]*", ms.MatchStart + ms.MatchLength);
    ms.GetMatch(change);
  }

  if (clientS.connected()) {  // disconnect from client
    clientS.stop();
  }

  // The stock price change should start with a + or -. Add the + for a
  // positive price move (since a - will already be there).
  String dir = change[0] != '-' ? "+" : "";

  chopDecimalPlaces(price);  // make sure 2 decimal places max
  chopDecimalPlaces(change); // make sure 2 decimal places max

  // Replace element in stocksQuotes with string containing the quote data
  stocksQuotes[index] = String(stocks[index] + " " + price + " " + dir + change);

  //Serial.println(stocksQuotes[index].c_str()); // for testing
  //Serial.print("Time Elapsed: "); // for testing
  //Serial.println(millis() - startTime); // for testing
} //getQuote

void TaskDisplay(void* pvParameters) {
  /**
    Task that handles constantly displaying stock quote data as a ticker tape.
    @param pvParameters a value that passed to the task - Not used.
    @return nothing.
  */
  
  // Load s string that contains all the stock quote data so we can display it
  String displayText = "";
  for (int i = 0; i < stocksLen; i++) {
    if (stocksQuotes[i].length() == 0) {
      displayText += updatingMsg;
      haveQuoteData = false; // we don't yet have all quotes so set flag to try again
    }
    else  {
      displayText += stocksQuotes[i] + spacer;
    }
  }

  int    quoteCnt = 0;  // counts the number of quotes displayed
  int    index    = 0;  // index in stocks array
  int    nPos     = 0;  // position in nextUp string
  String nextUp   = stocksQuotes[index] + spacer; // next quote to display

  display.setFont(ArialMT_Plain_24);

  for (;;) { // repeat forever

    //Serial.print("TaskDisplay running on core ");  // for testing
    //Serial.println(xPortGetCoreID());              // for testing

    displayText.remove(0, 1);
    displayText += nextUp.charAt(nPos);

    nPos += 1;

    if (nPos == nextUp.length()) {
      nPos = 0;
      index = (index + 1) % stocksLen;

      // Display the date & time after a certain number of quotes have have been displayed.
      quoteCnt = (quoteCnt + 1) % DISP_DATE_CNT; // reset counter
      if (quoteCnt == 0) {
        time_t      now;
        struct tm*  timeinfo;
        time(&now);
        timeinfo = localtime (&now);
        char dateTime[30];
        strftime (dateTime, 80, " ... %B, %d %Y  %I:%M %p ...   ", timeinfo);
        nextUp = dateTime;  // add date and time to ticker display
      }
      else if (stocksQuotes[index].length() == 0) {
        nextUp = updatingMsg;
      }
      else {
        nextUp = stocksQuotes[index] + spacer; // add next quote to ticker display
      }
    }

    display.clear();
    display.drawString(0, 16, displayText.substring(0, displayWidth));
    display.display();
    delay(200);
  }
}

void TaskGetQuotes(void* pvParameters) {
  /**
    Task that handles getting and updating stock quote data. Quote data is held in
    the stocksQuotes array.
    @param pvParameters a value that passed to the task - Not used.
    @return nothing.
  */
  int  stocksIndex = 0;
  
  for (;;) { // repeat forever

    // Get quote data only on scheduled interval
    if (millis() > lastUpdateTime + intervalUpdate) {

      //Serial.print("TaskGetQuotes running on core ");   // for testing
      //Serial.println(xPortGetCoreID());                 // for testing

      // Only get quote data one time when market is closed. Once we have read quotes
      // for all the stocks, then haveQuoteData is true so we don't needlessly update the
      // quote data again when the market is closed.
      if (marketOpen() || !haveQuoteData) {
        //Serial.printf("Getting quote for %s\n", stocks[stocksIndex].c_str());  // for testing
        getQuote(stocksIndex);
        stocksIndex += 1;
        if (stocksIndex == stocksLen) {
          stocksIndex = 0;
          lastUpdateTime = millis();
          haveQuoteData = true;
        }
      }
      else { // if market is closed and have loaded all quote data
        lastUpdateTime = millis();
      }
    }
    delay(100);
  } // for loop
}


void setup()
{
  // Initialize the display
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50);
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high

  display.init();
  display.setFont(ArialMT_Plain_16);

  Serial.begin(115200);
  Serial.println();
  Serial.println();
  delay(10);

  // If no stock tickers in the list then return error
  if (stocksLen == 0) {
    Serial.println("ERROR: Empty stocks array.");
    return;
  }

  // Initialize the quote data array
  for (int i = 0; i < stocksLen; i++) {
    stocksQuotes[i] = "";
  }

  // Connect to a WiFi network
  wifiMulti.addAP(wifiSSID01, wifiPassword01);
  wifiMulti.addAP(wifiSSID02, wifiPassword02);

  //Serial.println("\nConnecting to WiFi... ");  // for testing
  display.drawStringMaxWidth(0, 6, 128, "Connecting to WiFi...");
  display.display();

  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
    //Serial.print(".");
  }

  display.clear();
  String wifiStatus = String("WiFi connected at: ") + (char*) WiFi.localIP().toString().c_str();
  //Serial.print(wifiStatus); // for testing
  display.drawStringMaxWidth(0, 6, 128, wifiStatus);
  display.display();

  // Set the time zone. Most of USA/Canada observes DST from the second Sunday in March
  // to the first Sunday in November. The line of code below sets the time zone and
  // DST information for Eastern Standard Time.
  // For setenv info see: https://users.pja.edu.pl/~jms/qnx/help/watcom/clibref/global_data.html
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0/02:00:00,M11.1.0/02:00:00", 1);
  tzset();
  delay(2000);

  if (!marketOpen()) {
    display.clear();
    display.drawStringMaxWidth(0, 6, 128, "Market is closed.");
    display.display();
    delay(2000);
  }

  display.clear();
  display.drawStringMaxWidth(0, 6, 128, "Fetching Data...");
  display.display();

  // Create two tasks, one on each core. One task handles displaying data and the other task
  // handles scraping quote data from a website.
  xTaskCreatePinnedToCore(
    TaskGetQuotes,    /* Function to implement the task */
    "TaskGetQuotesH", /* task handle */
    10000,            /* Stack size in words */
    NULL,             /* Task input parameter */
    0,                /* Priority of the task */
    NULL,             /* Task handle. */
    0);               /* Core where the task should run */

  delay(30000); // allow time to get some quote data

  xTaskCreatePinnedToCore(
    TaskDisplay,      /* Function to implement the task */
    "TaskDisplayH",   /* Task handle */
    10000,            /* Stack size in words */
    NULL,             /* Task input parameter */
    2,                /* Priority of the task */
    NULL,             /* Task handle. */
    1);               /* Core where the task should run */

} // setup()

void loop()
{
  delay(100);  // this should prevent "Task watchdog triggeed" error.
} // loop()
