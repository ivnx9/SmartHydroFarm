
//  app.LoadScript( "https://smarthydrofarm.com/api/appUI.js" )
 
  app.LoadScript( "loadScript.js" )
 
//Called when application is started.
function OnStart()
{    

    
   // update appUI
  //  app.ShowProgress( "Checking for Updates.." )
     var checkUpdate = app.HttpRequest( "GET", "https://ivnx9.github.io/SmartHydroFarm/config.json", null, null, callback_version )

// call it
// loadConfig(callback_version);

   // callback if the user has no internet
    
 
    //Lock screen orientation to Portrait.
    app.SetOrientation( "Portrait" )

	//Create the main app layout.
	layMain = app.CreateLayout( "Linear", "Vertical,FillXY" );
	layMain.SetBackColor( "#454545" );

   	//Create the main app layout.
	layLoadingScreen= app.CreateLayout( "Linear", "Vertical,FillXY" );
	layLoadingScreen.SetBackGradient( "#000000", "#454545", "#000000");
  layMain.AddChild( layLoadingScreen )

  Displaylogo = app.CreateImage( "Img/SmartHydroFarm.png", 0.3,0.15)
  Displaylogo.SetMargins( 0, 0.35, 0,0)
  layLoadingScreen.AddChild( Displaylogo )

   //Create second image.
	img = app.CreateImage( "Img/loading.png", 0.4, -1)
	img.Scale( 0.3, 0.3 )
	img.Move( 0, 0.15 )
  img.SetMargins(0,-0.03,0, -0.025)
	layLoadingScreen.AddChild( img )

  DisplayText = app.CreateText( "Loading...", 0.5, 0.05,  "AutoScale" )
  DisplayText.SetTextSize( 20 )
 layLoadingScreen.AddChild( DisplayText )

  
   //Start timer to rotate top image.
//	setInterval( "RotateImage()", 150)
RotateImage()
  
  main()





}
var angle=0;
function RotateImage( ev )
{
	img.Rotate( angle )
	angle += 80;
 if(layLoadingScreen.IsEnabled() == true){
  setTimeout("RotateImage()", 150);
   }
}


function loadConfig(callback_version) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "https://ivnx9.github.io/SmartHydroFarm/config.json", true);

  xhr.timeout = 10000; // optional: 10s timeout

  xhr.onreadystatechange = function () {
    if (xhr.readyState !== 4) return;

    const reply = xhr.responseText;
    const status = xhr.status;

    // HTTP error (404, 500, etc.)
    if (status < 200 || status >= 300) {
      callback_version(new Error("HTTP Error: " + status), reply, status);
      return;
    }

    // Success
    callback_version(null, reply, status);
  };

  // Network error (no internet, CORS blocked, DNS, etc.)
  xhr.onerror = function () {
    callback_version(new Error("Network Error"), "", 0);
  };

  // Timeout error
  xhr.ontimeout = function () {
    callback_version(new Error("Timeout"), "", 0);
  };

  xhr.send();
}

// ==========

function callback_version(error, reply, status)
{

	if(error){  noInternet();  }
  else {
    const config = JSON.parse(reply)
     //app.Alert(JSON.stringify(config, null, 2))
    if(isJson(app.ReadFile( "config.json" )) == true) {var cver = JSON.parse(app.ReadFile( "config.json" )); }
    else {var cver = {"version":"0.0.0"}; }
     //app.Alert( config.version+" "+cver.version )
   
       if(config.version != cver.version) {
         DisplayText.SetText( "Updating app..." )
  const merged = [...config.appjs, ...config.webpage]
      //  app.Alert( JSON.stringify(merged, null, 2) )
 fetchPages(config, JSON.stringify(merged));
// fetchPages(config, config.testB);

      
       //  var update_appUI =  app.HttpRequest( "GET", "https://smarthydrofarm.com/api/appUI.js", null, null, callbacks  )
        } else {
          layLogin.Animate("FadeIn")
        layLoadingScreen.Animate("FadeOut") 
       app.ShowPopup( " Authenticating..." )
          layLoadingScreen.SetEnabled( false  )
          setTimeout(  InitAuth, 1000)
      
      }
   }
}

function isJson(value) {
  if (typeof value !== "string") return false;
  try {
    JSON.parse(value);
    return true;
  } catch {
    return false;
  }
}



function fetchPages(config, pages, index = 0) {
    if (index >= pages.length) {
        //app.ShowPopup("All files downloaded!.");
         
          app.WriteFile( "config.json", JSON.stringify(config, null, 2) )
         app.ShowPopup( "App has been updated to "+config.version )
       layLogin.Animate("FadeIn")
       layLoadingScreen.Animate("FadeOut")
        layLoadingScreen.SetEnabled( false  )
        InitAuth()
  
        return;
    }
    //app.Alert(pages);
    let page = JSON.parse(pages)[index];
    

    app.HttpRequest(
        "GET",
        config.host + config.PATH + "/" + page,
        null,
        null,
        (error, reply, status) => {

            if (error) {
                app.Alert("Something went wrong. Trying to update again. ");
              //  app.Exit()
            //    app.LaunchApp( "com.smarthydrofarm.shvf" , true)
              //  var action = "android.intent.action.MAIN";
             //  app.SendIntent( this.packageName, this.className, action ) 
                fetchPages(config, pages, reply, index = 0) 
            } else {
                app.WriteFile(page, reply);
                app.Debug("Saved: " + page);
            }

            //  load next file AFTER this finishes
            fetchPages(config,pages, index + 1);
        }
    );
}