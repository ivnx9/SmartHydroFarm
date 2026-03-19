   function getVersionApp() {
       return "0.1.3";
   }
   
  function dashboardUI()
{

   dashboardWebView = app.CreateWebView( 1, 0.95,"NoActionBar,NoScrollBars, NoLongTouch")
   dashboardWebView.LoadUrl( "dboard.html" ) 
   layContent_Dashboard.AddChild( dashboardWebView )
}

function control_and_configUI() 
{
   controlWebView = app.CreateWebView( 1, 0.95,"NoActionBar,NoScrollBars, NoLongTouch")
    controlWebView.LoadUrl( "control_config.html" ) 
   layContent_Control_config.AddChild( controlWebView )
}

/*
   layContent_Control_config = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_Webhooks = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_Chatbot = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_About = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_Settings = app.CreateLayout( "Linear", "VCenter, FillXY" )

*/
     function webhooksUI()
{
 
  // layContent_Webhooks

  webhooksWebView = app.CreateWebView( 1, 0.95,"NoActionBar,NoScrollBars, NoLongTouch")
    webhooksWebView.LoadUrl( "webhooks.html" ) 
   layContent_Webhooks.AddChild( webhooksWebView )

   
}
     function chatbotUI()
{
    // layContent_Chatbot
   chatbotWebView = app.CreateWebView( 1, 0.95,"NoActionBar,NoScrollBars, NoLongTouch")
    chatbotWebView.LoadUrl( "chatbot.html" ) 
   layContent_Chatbot.AddChild( chatbotWebView )
}
     function aboutUI()
{
    aboutWebView = app.CreateWebView( 1, 0.95,"NoActionBar,NoScrollBars, NoLongTouch")
    aboutWebView.LoadUrl( "about.html" ) 
   layContent_About.AddChild( aboutWebView )
}
     function settingsUI()
{
   // layContent_Settings
}

 function renderGauge()
{
   
  //  Ph, TDSwater temp, water level, humidity, air temp,
    /*
     ph                      |    TDS
     Water Temp    |    water level
      air temp          |  Humidity
 */
 // gauge1.LoadUrl( "gauge.html" )

  if(gauge1.GetVisibility() == "Show" ){
      	gauge1.Execute( "if(typeof DrawGaugePh === 'function') DrawGaugePh( 'g_ph',  'pH Level' ); ");
    	  gauge1.Execute( "if(typeof DrawGaugeTds === 'function') DrawGaugeTds('g_tds',   'TDS Level' ); " );
      	gauge1.Execute( "if(typeof DrawGauge_WaterTemp === 'function') DrawGauge_WaterTemp('g_waterTemp', 'Water Temperature'); " );
      	gauge1.Execute( "if(typeof DrawGaugeWaterLevel === 'function') DrawGaugeWaterLevel('g_waterLevel', 'Water Level'); " );
      	gauge1.Execute( "if(typeof DrawGauge_Air === 'function') DrawGauge_Air('g_air', 'Temp'); " );
      	gauge1.Execute( "if(typeof DrawGaugeHumidity === 'function') DrawGaugeHumidity('g_humidity', 'Humidity');  " );

        setTimeout(  StartGaugeAutoUpdate, 1000);
  }
}
/* ===== OPTIONAL DEMO INIT (remove if main code calls your draw functions) =====
    document.addEventListener("DOMContentLoaded", () => {
  DrawGaugePh("g_ph", "pH Level");
  DrawGaugeTds("g_tds", "TDS Level");
  DrawGauge_WaterTemp("g_waterTemp", "Water Temp");
  DrawGaugeWaterLevel("g_waterLevel", "Water Level");
  DrawGauge_Air("g_air", "Air Temp");
  DrawGaugeHumidity("g_humidity", "Humidity");

  // Example values
  setGaugeValue("g_ph", 6.4);
  setGaugeValue("g_tds", 850);
  setGaugeValue("g_waterTemp", 30);
  setGaugeValue("g_waterLevel", 120);
  setGaugeValue("g_air", 27);
  setGaugeValue("g_humidity", 55);
});
*/

// ====== CONFIG ======
var API_BASE = "https://smarthydrofarm.com/api/webhook.php";  // change if needed (full URL recommended)
var POLL_MS  = 1500;               // 1.5s (adjust)

// Active device creds (set these from your app)
var DEVICE_ID   = "SHVF-V1";
var DEVICE_CODE = "12345";

// Cache last sent values so it only updates when changed
var lastVals = {
  ph: null, tds: null, waterTemp: null, waterLiters: null, air: null, hum: null
};

// ====== Call this once after gauge1 is ready ======
function StartGaugeAutoUpdate() {
  StopGaugeAutoUpdate();     // avoid duplicates
  PollDashboardOnce();       // immediate
  _pollTimer = setInterval(PollDashboardOnce, POLL_MS);
}

function StopGaugeAutoUpdate() {
  if (typeof _pollTimer !== "undefined" && _pollTimer) {
    clearInterval(_pollTimer);
    _pollTimer = null;
  }
}

// ====== Fetch panel from DB via webhook.php ======
function PollDashboardOnce() {
  var url = API_BASE + "?cmd=panel"
    + "&device_id=" + encodeURIComponent(DEVICE_ID)
    + "&device_code=" + encodeURIComponent(DEVICE_CODE);

  // DroidScript: Ajax(url, callback)
  app.HttpRequest("GET", url, null, null, function(error, reply) {
    if (error) {
      // optional: app.Debug / app.ShowPopup
      // app.Debug("Poll error: " + error);
      return;
    }

    var data = null;
    try {
      data = JSON.parse(reply);
    } catch (e) {
      // app.Debug("Bad JSON: " + reply);
      return;
    }

    // Extract latest sensor values (from your dashboard structure)
    var s = (data && data.latest_sensor) ? data.latest_sensor : {};

    // Your DB keys in the dashboard:
    // ph, tds, water_temp, air_temp, humidity, drumLiters
    var nowVals = {
      ph:         safeNum(s.ph),
      tds:        safeNum(s.tds),
      waterTemp:  safeNum(s.water_temp),
      waterLiters:safeNum(s.drumLiters),
      air:        safeNum(s.air_temp),
      hum:        safeNum(s.humidity)
    };

    // Update ONLY if changed
    ApplyGaugeChanges(nowVals);
  });
}

// ====== Only update when changed ======
function ApplyGaugeChanges(v) {
  // if gauge1 not ready, skip
  if (!gauge1) return;

  // ph
  if (v.ph !== null && v.ph !== lastVals.ph) {
    gauge1.Execute("setGaugeValue('g_ph'," + v.ph + ")");
    lastVals.ph = v.ph;
  }

  // tds
  if (v.tds !== null && v.tds !== lastVals.tds) {
    gauge1.Execute("setGaugeValue('g_tds'," + v.tds + ")");
    lastVals.tds = v.tds;
  }

  // water temp
  if (v.waterTemp !== null && v.waterTemp !== lastVals.waterTemp) {
    gauge1.Execute("setGaugeValue('g_waterTemp'," + v.waterTemp + ")");
    lastVals.waterTemp = v.waterTemp;
  }

  // water level (liters)
  if (v.waterLiters !== null && v.waterLiters !== lastVals.waterLiters) {
    gauge1.Execute("setGaugeValue('g_waterLevel'," + v.waterLiters + ")");
    lastVals.waterLiters = v.waterLiters;
  }

  // air temp
  if (v.air !== null && v.air !== lastVals.air) {
    gauge1.Execute("setGaugeValue('g_air'," + v.air + ")");
    lastVals.air = v.air;
  }

  // humidity
  if (v.hum !== null && v.hum !== lastVals.hum) {
    gauge1.Execute("setGaugeValue('g_humidity'," + v.hum + ")");
    lastVals.hum = v.hum;
  }
}

// ====== Helpers ======
function safeNum(x) {
  if (x === undefined || x === null || x === "") return null;
  var n = Number(x);
  return isNaN(n) ? null : n;
}


function setVal()
{
   
	  StartGaugeAutoUpdate()
/*
   gauge1.Execute( "setGaugeValue('g_ph', " + (value) + ")" );
   gauge1.Execute( "setGaugeValue('g_tds', " + (value) + ")" );
   gauge1.Execute( "setGaugeValue('g_waterTemp', " + (value) + ")" );
   gauge1.Execute( "setGaugeValue('g_waterLevel', " + (value) + ")" );
   gauge1.Execute( "setGaugeValue('g_air', " + (value) + ")" );
   gauge1.Execute( "setGaugeValue('g_humidity', " + (value) + ")" );

  */
}


/*
//Called when user touches seek bar.
function skb_OnTouch( value )
{
	if( spinStyle.GetText()=="Speed" ) 
		web.Execute( "gauge.setValue(" + (220*value/100) + ")" );
		
	else if( spinStyle.GetText()=="Temperature" ) 
		web.Execute( "gauge.setValue(" + (value-50) + ")" );
		
	else if( spinStyle.GetText()=="Delay" ) 
		web.Execute( "gauge.setValue(" + (value*10) + ")" );
	else 
		web.Execute( "gauge.setValue(" + (value) + ")" );
}


*/