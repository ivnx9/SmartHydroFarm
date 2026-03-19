function main()
{

	//Create the main app layout.
	layLogin = app.CreateLayout( "Linear", "Vertical,FillXY" );
	layLogin.SetBackColor( "#454545" );
  layLogin.SetVisibility( "Hide" ) 

  
logo = app.CreateImage( "Img/SmartHydroFarm.png", 0.3,0.15)
  logo.SetMargins( 0, 0.2, 0,0)
  layLogin.AddChild( logo )

  username = app.CreateTextEdit( "", 0.8, 0.05, "SingleLine" )
//  username.SetBackColor( "#dddddd" )
 // username.SetBackground( "clear.png" )
  username.SetBackColor( "#dddddd" )
  username.SetTextColor( "#000000" )
  username.SetHint( "Username.." )
  username.SetMargins( 0, 0.08, 0,0)
  layLogin.AddChild( username )

  password = app.CreateTextEdit( "", 0.8, 0.05, "SingleLine,Password")
  password.SetBackColor( "#dddddd" )
  password.SetTextColor( "#000000" )
  password.SetHint( "Password.." )
  password.SetMargins( 0, 0.02, 0,0.08)
  layLogin.AddChild( password )

  loginBtn = app.CreateButton( "Login", 0.8, 0.07, "Custom")
  loginBtn.SetOnTouch( loginBtn_OnTouch ) 
  loginBtn.SetStyle( "#4285F4", "#4285F4", 20, null ,0, 0.2)
  layLogin.AddChild( loginBtn )
  
  RegisterAcc = app.CreateText( "No Account? Register here.", 0.45, 0.25, "" )
  RegisterAcc.SetOnTouch( () => { app.OpenUrl( "https://smarthydrofarm.com" )} )
  RegisterAcc.SetMargins(0, 0.03, 0,0)
  layLogin.AddChild( RegisterAcc )

   	//Create the main app layout.

	layHome = app.CreateLayout( "Linear", "FillXY" );
	layHome.SetBackColor( "#ffffff" );
  layHome.SetVisibility( "Hide" )

   


  tabs = app.CreateCustomTabs()
   	//Create an 'Action Bar' and drawer.
    CreateActionBar()
	  CreateDrawer()
	
    	//Create the app content layout.
  	layPage= app.CreateScroller( 1, 1, "FillY,NoScrollBar")
    layPage.SetSize( 1, 1 );
   layHome.AddChild( layPage )

    //Create the app content layout.
  	layContent_Dashboard = app.CreateLayout( "Linear", "VCenter, FillXY" )
   layPage.AddChild( layContent_Dashboard )

    layContent_Control_config = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_Webhooks = app.CreateLayout( "Linear", "VCenter, FillXY" )
   // layPage.AddChild( layContent_Webhooks)
    layContent_Chatbot = app.CreateLayout( "Linear", "VCenter, FillXY" )
    layContent_About = app.CreateLayout( "Linear", "VCenter, FillXY" )
  // layPage.AddChild( layContent_About)
    layContent_Settings = app.CreateLayout( "Linear", "VCenter, FillXY" )
//   layContent_Webhooks.child

 //   layPage.AddChild( layContent_Webhooks )


     CreateContent()
   //Create main content.
    
    	//Add main layout and drawer to app.	
	app.AddLayout( layMain ) // Do not remove this
  app.AddLayout( layLogin )
  app.AddLayout( layHome )
}







function noInternet()
{ 
     var noNet = app.CreateYesNoDialog("Oops!! Looks like you're not connected to internet, Do you want to try offline mode? make sure you're connected to SmartHydroFarm system's Wi-Fi.")
     
     noNet.SetOnTouch( noNet_OnTouch )
     noNet.Show()
  return;
 
}


// call back if no inernet
function noNet_OnTouch(result) {
    if (result == "Yes")  { tabs = app.CreateCustomTabs(); tabs.OpenUrl("http://192.168.4.1"); }
    if(result == "No") { app.Alert("Please check your internet connection then try again later."); app.Exit(); }
}


// Call this inside your OnStart() after layouts are ready
function InitAuth()
{
    LoadToken();
// 0970 remove the true to get back the functionality
// setTimeout( ()=>{
    if  (IsTokenValid(token)) {
      //  grantLogin();
       app.ShowProgress( "Logging in.." )
        app.ShowPopup("Logged in (saved session).");
        LoadMe(LoadData)
    } else {
        if (token)  ClearToken(); // had token but expired/invalid 
          
        if(token !== null && token !== "") {
         app.ShowPopup( "Token has expired. Please login again." ); 
       //  ClearToken(); 
          }
         else {   app.ShowPopup( "Welcome to SmartHydroFarm!") }
           
    }
   // }, 500);

}



// ================

// --- LOGIN ---
function loginBtn_OnTouch() {
    usernme = username.GetText();
    var passwd = password.GetText();

    var url = "https://smarthydrofarm.com/api/app-login.php";
    app.HttpRequest("GET", url, null, "username="+usernme+"|password="+passwd, handleLoginReply);
}

function handleLoginReply(error, reply, status)
{
    if (error) {
        app.Alert("Request failed:\n" + reply + "\nStatus: " + status);
        return;
    }

    var obj;
    try { obj = JSON.parse(reply); }
    catch (e) {
        app.Alert("Invalid JSON reply.\n\nRaw:\n" + reply);
        return;
    }

    // Handle based on your PHP structure
    if (obj.status === "success" && obj.token) {
        token = obj.token;
        SaveToken(token);

        // Safety: ensure token has exp and not expired
        if (!IsTokenValid(token)) {
            ClearToken();
            app.ShowPopup("Login token expired/invalid.");
            return;
        }

        grantLogin();
        app.ShowPopup("Access Granted.");
        return;
    }

    if (obj.status === "unverified") {
        // Do NOT save token (none is provided)
        ClearToken();

        // If you want to open OTP page in browser/webview:
        // (redirect is like "confirm_otp.html?email=...&user_id=...&username=...")
        if (obj.redirect) {
            // If redirect is relative path, make it absolute:
            var otpUrl = obj.redirect;
            if (otpUrl.indexOf("http") !== 0) otpUrl = "https://smarthydrofarm.com/" + otpUrl;

            app.ShowPopup(obj.message || "Account not activated.");
            // Option A: open in browser
            tabs.OpenUrl(otpUrl);

            // Option B: if you have a WebView screen, load otpUrl there instead
        } else {
            app.ShowPopup(obj.message || "Account not activated.");
        }
        return;
    }

    // Default: error
    ClearToken();
    app.ShowPopup(obj.message || "Login failed.");
}


// ============================
// TOKEN STORAGE (SaveText/LoadText)
// ============================

function SaveToken(tkn) {
    token = tkn || "";
    try { app.SaveText(TOKEN_KEY, token);  } catch(e) {}
}

function LoadToken() {
    try { token = app.LoadText(TOKEN_KEY, ""); } catch(e) { token = ""; }
}

function ClearToken() {
    token = "";
    try { app.SaveText(TOKEN_KEY, ""); 
    app.SaveText("shvf_device_id", "");
    app.SaveText("shvf_device_code", "");
    } catch(e) {}
}


// ============================
// TOKEN VALIDATION (exp check)
// ============================

function IsTokenValid(tkn) {
    if (!tkn || typeof tkn !== "string") return false;

    var payload = GetJwtPayload(tkn);
    if (!payload) return false;

    // Your PHP always sets exp, so require it
    if (!payload.exp) return false;

    var nowSec = Math.floor(Date.now() / 1000);
    var leeway = 10;

    return payload.exp > (nowSec + leeway);
}

function GetJwtPayload(jwt) {
    var parts = jwt.split(".");
    if (parts.length < 2) return null;

    var jsonStr = Base64UrlDecode(parts[1]);
    if (!jsonStr) return null;

    try { return JSON.parse(jsonStr); } catch(e) { return null; }
}

function Base64UrlDecode(b64url) {
    var b64 = b64url.replace(/-/g, "+").replace(/_/g, "/");
    while (b64.length % 4 !== 0) b64 += "=";
    return Base64DecodeToString(b64);
}

function Base64DecodeToString(b64) {
    if (typeof atob === "function") {
        try { return atob(b64); } catch(e) {}
    }

    var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    var output = "", buffer = 0, bits = 0;

    for (var i = 0; i < b64.length; i++) {
        var c = b64.charAt(i);
        if (c === "=") break;

        var val = chars.indexOf(c);
        if (val < 0) continue;

        buffer = (buffer << 6) | val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output += String.fromCharCode((buffer >> bits) & 0xFF);
        }
    }
    return output;
}


// ============================
// OPTIONAL: AUTHED API CALLS
// ============================
// Your server currently only sets CORS headers for login.php.
// If you want to call protected endpoints from the app,
// you'll need those endpoints to also allow Authorization header:
//   header("Access-Control-Allow-Headers: Content-Type, Authorization");

function ApiGet(url, cb) {
    if (!IsTokenValid(token)) {
        ClearToken();
        app.ShowPopup("Session expired. Please login again.");
        return;
    }

    var headers = "Authorization: Bearer " + token;
    app.HttpRequest("GET", url, headers, null, cb);
}



 function grantLogin() { 
app.HideKeyboard(); 
 layHome.Animate( "FadeIn");	
app.AddDrawer( drawerScroll, "Left", drawerWidth ) 
 //  if(webhooksWebView.GetVisibility() == "Show") {
    //   webhooksWebView.Execute( "if (typeof setJwtToken === 'function') {setJwtToken('"+app.LoadText(TOKEN_KEY, "", "token")+"')}")
  // app.Alert( app.LoadText(TOKEN_KEY, "", "token") )
   // }
}





/*   ============ HOME LAYOUT ========== */

//Create the main app content.
function CreateContent()
{
   	dashboardUI()
     control_and_configUI()
     webhooksUI()
     chatbotUI()
     aboutUI()
     settingsUI()
}

//Create an action bar at the top.
function CreateActionBar()
{
    //Create Card layout for top bar (with drop shadow).
    layBar = app.CreateLayout( "Card", "Horizontal,FillX" )
    layBar.SetElevation( 10 )
    //layBar.SetMargins( 0, 0.3, 0,0)
    layBar.SetBackColor( "#252525") // "#4285F4" )
    layHome.AddChild( layBar )

    //Add title text.
    txtBarTitle = app.AddText( layBar, "Dashboard", 1 )
    txtBarTitle.SetPadding( 12,10,12,10, "dip" )
    txtBarTitle.SetTextSize( 22 )
    txtBarTitle.SetTextColor( "#ffffff" )
    
    //Create title layout.
    layBarTitle = app.AddLayout( layBar, "Linear", "Horizontal,Left" )
    
    //Add hamburger icon .
    txtBurger = app.AddText( layBarTitle, "[fa-bars]", -1,-1, "FontAwesome" )
    txtBurger.SetPadding( 12,12,12,10, "dip" )
    txtBurger.SetTextSize( 24 )
    txtBurger.SetTextColor( "#eeeeee" )
    txtBurger.SetOnTouchUp( function(){app.OpenDrawer()} )
    /*
    //Add menu icon.
    txtMenu = app.AddText( layBarTitle, "[fa-ellipsis-v]", -1,-1, "FontAwesome,Right" );
    txtMenu.SetPadding( 12,14,16,10, "dip" )
    txtMenu.SetMargins( 0.8 )
    txtMenu.SetTextSize( 24 )
    txtMenu.SetTextColor( "#eeeeee" );
    txtMenu.SetOnTouchUp( function(){app.ShowPopup("Todo!")} )
*/
}

//Create the drawer contents.
function CreateDrawer()
{
    //Create a layout for the drawer.
	//(Here we also put it inside a scroller to allow for long menus)
	drawerWidth = 0.75;
    drawerScroll = app.CreateScroller( drawerWidth, -1, "FillY" )
    drawerScroll.SetBackColor( "White" )
	layDrawer = app.CreateLayout( "Linear", "Left" )
	drawerScroll.AddChild( layDrawer )
	
	//Create layout for top of drawer.
	layDrawerTop = app.CreateLayout( "Absolute" )
	layDrawerTop.SetBackColor( "#252525" )
	layDrawerTop.SetSize( drawerWidth, 0.23 )
	layDrawer.AddChild( layDrawerTop )
	
	//Add an icon to top layout.
	var img = app.AddImage( layDrawerTop, "Img/SmartHydroFarm.png", 0.15 )
	img.SetPosition( drawerWidth*0.06, 0.08 )
	
	//Add user name to top layout.
	txtUser = app.CreateText(  usernme,-1,-1,"Bold")
	txtUser.SetPosition( drawerWidth*0.07, 0.155 )
	txtUser.SetTextColor( "White" )
	txtUser.SetTextSize( 13.7, "dip" )
  layDrawerTop.AddChild( txtUser )
	
	//Add user email to top layout.
	txtEmail = app.AddText( layDrawerTop, "smarthydrofarm.com")
	txtEmail.SetPosition( drawerWidth*0.07, 0.185 )
	txtEmail.SetTextColor( "#bbffffff" )
	txtEmail.SetTextSize( 14, "dip" )
	
	//Create menu layout.
	var layMenu = app.CreateLayout( "Linear", "Left" )
	layDrawer.AddChild( layMenu )
	

/*

Based on your hint with [fa-home], I'm assuming you're using Font Awesome icons. Here are some suggestions for each of the options:

- *Dashboard*: [fa-tachometer-alt]
- *Control and configuration*: [fa-cog] or [fa-sliders-h]
- *Webhooks*: [fa-link] or [fa-plug]
- *About*: [fa-info-circle]
- *Settings*: [fa-cog]

These icons are commonly used for these types of functions and should fit well with your existing design [1].




*/
    //Add a list to menu layout (with the menu style option).
    var listItems = "Dashboard::[fa-home],Control&Configuration::[fa-sliders],API Webhooks::[fa-link]";
    lstMenu1 = app.AddList( layMenu, listItems, drawerWidth, -1, "Menu,Expand" )
    lstMenu1.SetColumnWidths( -1, 0.35, 0.18 )
    lstMenu1.SelectItemByIndex( 0, true )
    lstMenu1.SetItemByIndex( 0, "Dashboard", 21 )
    lstMenu1.SetOnTouch( lstMenu_OnTouch )
    
    //Add seperator to menu layout.
    var sep = app.AddImage( layMenu, null, drawerWidth,0.001,"fix", 2,2 )
    sep.SetSize( -1, 1, "px" )
    sep.SetColor( "#cccccc" )
    
    //Add title between menus.
	txtTitle = app.AddText( layMenu, "others",-1,-1,"Left")
	txtTitle.SetTextColor( "#666666" )
	txtTitle.SetMargins( 16,12,0,0, "dip" )
	txtTitle.SetTextSize( 14, "dip" )
	
/*
For Font Awesome icons, here are some suggestions for chatbot or AI:
- *Chatbot*: [fa-robot]
- *A.I.*: [fa-brain]
- *Chat*: [fa-comment]
- *Bot*: [fa-android] or [fa-robot]

These icons can represent artificial intelligence, chatbots, or automated systems in your application or website [1].

*/

    //Add a second list to menu layout.      ,Settings::[fa-cog]
    var listItems = "Chatbot::[fa-comment],About::[fa-info-circle],Logout::[fa-sign-out]";
    lstMenu2 = app.AddList( layMenu, listItems, drawerWidth, -1, "Menu,Expand" )
    lstMenu2.SetColumnWidths( -1, 0.35, 0.18 )
    lstMenu2.SetOnTouch( lstMenu_OnTouch )
}





//Handle menu item selection.
function lstMenu_OnTouch( title, body, type, index )
{
    //Close the drawer.
    app.CloseDrawer( "Left" )
    
    if(title=="Logout"){
    layHome.Animate( "FadeOut" )
    layHome.SetVisibility("Hide")
    app.RemoveDrawer( drawerScroll )
    // app.SaveText(TOKEN_KEY, null);
    ClearToken();
    }
    
    //Highlight the chosen menu item in the appropriate list.
    if( this==lstMenu1 ) lstMenu2.SelectItemByIndex(-1)
    else lstMenu1.SelectItemByIndex(-1)
    this.SelectItemByIndex( index, true )
    
    //Update title and page contents.
      txtBarTitle.SetText( title )
//     txtIcon.SetText( type )
 //   txtContent.SetHtml( "<p><font color=#4285F4><big>"+title+"</big></font></p>" )
    //     var listItems = "Dashboard::[fa-home],Control&Configuration::[fa-sliders],API Webhooks::[fa-link]";
  
    if(title == "Dashboard") {
   layPage.RemoveChild( layContent_Control_config )
   layPage.RemoveChild( layContent_Webhooks)
   layPage.RemoveChild( layContent_Chatbot )
   layPage.RemoveChild( layContent_About )
   layPage.RemoveChild( layContent_Settings )

   layPage.AddChild(  layContent_Dashboard )

 
   } else if (title== "Control&Configuration"){
       layPage.RemoveChild(layContent_Dashboard)
       layPage.RemoveChild( layContent_Webhooks)
       layPage.RemoveChild( layContent_Chatbot )
       layPage.RemoveChild( layContent_About )
       layPage.RemoveChild( layContent_Settings )

       layPage.AddChild( layContent_Control_config)
  } else if(title=="API Webhooks") {
   layPage.RemoveChild(layContent_Dashboard)
   layPage.RemoveChild( layContent_Control_config )
   layPage.RemoveChild( layContent_Chatbot )
   layPage.RemoveChild( layContent_About )
   layPage.RemoveChild( layContent_Settings )

   layPage.AddChild( layContent_Webhooks )

  } else if(title== "Chatbot"){
  layPage.RemoveChild(layContent_Dashboard)
   layPage.RemoveChild( layContent_Control_config )
   layPage.RemoveChild( layContent_Webhooks)
   layPage.RemoveChild( layContent_About )
   layPage.RemoveChild( layContent_Settings )

   layPage.AddChild( layContent_Chatbot )
  }else if(title== "About"){
layPage.RemoveChild(layContent_Dashboard)
   layPage.RemoveChild( layContent_Control_config )
   layPage.RemoveChild( layContent_Webhooks)
   layPage.RemoveChild( layContent_Chatbot )
   layPage.RemoveChild( layContent_Settings )

   layPage.AddChild( layContent_About )
  } else if(title=="Settings"){
layPage.RemoveChild(layContent_Dashboard)
   layPage.RemoveChild( layContent_Control_config )
   layPage.RemoveChild( layContent_Webhooks)
   layPage.RemoveChild( layContent_Chatbot )
   layPage.RemoveChild( layContent_About )

   layPage.AddChild( layContent_Settings )
 }else {
    //   app.ShowPopup( "Invalid" )
  }
    console.log( title )
}


function LoadMe(cb) {
    if (!IsTokenValid(token)) {
        ClearToken();
        app.ShowPopup("Session expired. Please login again.");
        return;
    }

    var headers = "token=" + token;

    app.HttpRequest("GET", "https://smarthydrofarm.com/api/me.php",  null, headers,
        function(err, reply, status) {
            if (err) { app.Alert(reply, status); return; }

            var obj;
            try { obj = JSON.parse(reply); }
            catch(e){ app.Alert("Invalid JSON:\n" + reply); return; }

            if (obj.status === "success" && obj.user) {
                // Example: set drawer name

                usernme = obj.user.name;
                if (txtUser) txtUser.SetText(usernme);
                grantLogin();
                 //txtUser.SetText(obj.user.name);
                 
                 app.HideProgress()
                if (cb) cb(obj.user);
            } else if (obj.status === "unverified" && obj.redirect) {
                app.ShowPopup(obj.message || "Account not activated.");
                var otpUrl = obj.redirect;
                if (otpUrl.indexOf("http") !== 0) otpUrl = "https://smarthydrofarm.com/" + otpUrl;
                app.OpenUrl(otpUrl);
            } else {
                app.ShowPopup(obj.message || "Failed to load profile.");
            }
        }
    );
}
function LoadData(user)
{
 
//txtUser.SetText( usernme)
}


//Called when a drawer is opened or closed.
function OnDrawer( side, state )
{
    console.log( side + " : " + state )
}