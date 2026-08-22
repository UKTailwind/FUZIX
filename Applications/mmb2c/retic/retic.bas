''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' retic.bas
' (c) Geoff Graham 2023
  Const Ver = "1.3"
'
' Change Log
' V1.0 - Original Version
' V1.1 - Minor reformatting
' V1.2 - Fixed the allocation of I/O pins for the solid state relays
'        Fixed an error in calculating the day of week for watering
'        Fixed a bug that caused some web pages to be truncated.
'        Prevented the display of the RUN NOW or CONFIG buttons while a sprinkler schedule is running
' V1.3 - Usability fixes
'        Added ability to use SMTP2GP for email.
' PC3  - Ported to the Pico Computer 3 (compiled with mmbc/cc).
'        Every change is marked 'PC3: and listed in MIGRATION.md:
'        pins remapped to the PC3 I/O header, flow metering disabled
'        (counting inputs are not implemented yet), email via Gmail
'        over TLS, WEB TCP SERVER PORT 80 as a statement, and the
'        small source fixes a compiler requires.
'
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' IMPORTANT
'PC3: on the PC3 the machine joins its network from /etc/wifi.conf at
'PC3: boot (no OPTION WIFI), the server port is the WEB TCP SERVER
'PC3: PORT 80 statement below (no saved option), and the console is
'PC3: the serial console (no Telnet).
'
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
'
' NOTE TO ANYONE MODIFYING THE WEB PAGES
' There is an interplay between this program, the web pages and the browser.
' Using embedded string variables in the web pages the BASIC program can change the way
' the web page looks and acts.  For example, the variable RunNow() may contain the HTML
' code to insert a RUN NOW button in index.html or it may be an empty string which will
' display nothing.
' The web page may also include code which causes the browser to act in a certain way.
' An example of this is the automatic refresh of index.html.  The program will calculate
' when it wants the page to be refreshed and will set the variable NextUpdate accordingly.
' index.html contains a VB script with NextUpdate embedded in it.  On loading the web page
' the browser will execute the VB script which sets a timer for an automatic page refrest,
' the time of which is the value in NextUpdate.
'
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Option ESCAPE
Option EXPLICIT
Option Autorun On

Const true = 1
Const false = 0

Const WatchDogOn = true          ' disable for debugging


''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' User changeable constants
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
Const DisableLocationWarning = 0 ' set this to 1 to disable the "location not set" error
Const DefaultTimeZone = 10.0     ' this is the default timezone if the location is not set

Const UpperFtolerance = 50       ' the % increase in flow rate to trigger a fault
Const LowerFtolerance = 50       ' the % decrease in flow rate to trigger a fault

Const RainThreshild = 90         ' % forecast chance of rain required to skip a schedule

' API key for accessing Open Weather Map
' This key is shared between all users of this program and is limited to 60 queries/minute.
' This should not be a problem but you can easily get your own key and be inderpendent.
' To do this goto https://openweathermap.org/ and open a free account, then generate a key
' and replace the key below with your own key.
Const OWMKey = "73cd207244614965fc5ca3646bdd10ab"

''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Const LogToConsole = true   ' set to false if you do NOT want status messages printed to the console
Const LogToFile = false     ' set to true if you want status messages recorded in A:/LogCurrent.txt

Const NbrValves = 8         ' the number of valves - note this cannot be changed without editing the web pages
Const NbrSched = 8          ' the number of schedules - also this cannot be changed without editing the web pages

' I/O pins
'PC3: remapped to the PC3 I/O header (GP0-GP7, GP26, GP34-GP46).
'PC3: the valves take GP0-GP7 in order, the rain sensor keeps GP26.
'PC3: Counting inputs are not implemented yet, so the flow meter
'PC3: (Option Count / GP18 / SETPIN CIN) is disabled - leave the
'PC3: "Flow detection" checkbox OFF on the setup page.
'PC3: Option Count GP18, GP19, GP20, GP21
Const LED = MM.Info(pinno gp35)
Const RunLed = MM.Info(pinno gp36)
Const Master = MM.Info(pinno gp34)
Dim Integer Valve(NbrValves)=(MM.Info(pinno gp0),MM.Info(pinno gp1),MM.Info(pinno gp2),MM.Info(pinno gp3),MM.Info(pinno gp4),MM.Info(pinno gp5),MM.Info(pinno gp6),MM.Info(pinno gp7),MM.Info(pinno gp7))
'PC3: Const Flow = MM.Info(pinno gp18)
Const RainDetectInput = MM.Info(pinno gp26)
Const AbortRun = MM.Info(pinno gp37)

'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' general variables
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
Dim Integer i
Dim String s

Dim Integer RefreshTime, NextUpdate, NextUpdateIdx
Dim String StatusMsg, SavedMsg, ResetFlowMsg, CancelB
Dim Integer NextSchedule, AbortSchedule
Dim Integer SunRise, SunSet
Dim Float MaxTemp, MaxRain
Dim Integer ManualRun(NbrSched)
Dim Integer FaultDetected = true   ' the status LED flashes if this is true
Dim Integer AveF(NbrValves)        ' average flow rate

' error flags - these are set by various parts of the program and then processed by the subroutine ProcessErrors
Dim integer ErrConfig(NbrSched), ErrNTP, ErrTimezone, ErrSunRiseSet, ErrWeather, ErrEmail
Dim Integer ErrFlow(NbrValves)

' these are generated by ProcessErrors and are the WEB error msg and the email error msg
Dim string ErrorStatusMsg, EmailStatusMsg

' these are used on the Setup.html page to display the result of the tests
Dim String EmailTestMsg, LocationTestMsg, LocationDataMsg

''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' Configuration variables saved to A:/settings.dat
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
Dim String Title(NbrSched) Length 21               ' used only in the main page
Dim String Enabled(NbrSched) Length 24             ' shedule is enabled if the value <> ""

Dim String StartDay(NbrSched) Length 8             ' day of the month that the schedule becomes active
Dim String StartMonth(NbrSched) Length 8           ' as above but the month in the year
Dim String EndDay(NbrSched) Length 8               ' day of the month that the schedule stops being active
Dim String EndMonth(NbrSched) Length 8             ' as above but the month in the year

Dim String RunDOW(7,NbrSched) Length 8             ' days of the week when the schedule should run if the value <> ""
Dim String RunDays(NbrSched) Length 8              ' -or- run the schedule every this number of days

Dim String RunHours(NbrSched) Length 8             ' nbr of hours midnight/sunrise/sunset to run
Dim String RunMints(NbrSched) Length 8             ' as above but minutes
Dim String RunStart(NbrSched)  Length 8            ' start point (midnight, sunrise, etc) 0 to 4

Dim String ValveR(NbrValves,NbrSched) Length 8     ' the watering time for each valve

Dim String Rain(NbrSched) Length 8                 ' of the value <> "" then do not run if rain forecast
Dim String RunIncrease(NbrSched) Length 8          ' if hot weather forecast increase sprinkler time by this
Dim String TempThresh(NbrSched) Length 8           ' threshold in deg C for "hot weather"

' setup variables also saved to A:/settings.dat
Dim Float OWMlat = 9999,  OWMlong                  ' Location latitude & longtitude (9999 means not set)
Dim Float TimeZone                                 ' timezone of the location (this includes Daylight Saving adjustment)
Dim String OWMCity, OWMCountry                     ' location city and country code

Dim String FlowDetect, RainDetect                  ' sensor enable checkboxes

Dim String Smtp2GoUser, Smtp2GoPasswd, SGKey       ' SMTP relay credentials
Dim String EmailFrom, EmailTo                      ' email from and to fields
Dim String EMTime, EMW, EMFlow, EMStatus           ' circumstances when a warning email should be sent if the value <> ""


''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' start of the program
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
If WatchDogOn Then WatchDog 60000

' init all I/O pins
SetPin LED, DOUT
SetTick 300, FlashLED

SetPin RunLed, DOUT
SetPin Master, DOUT
'PC3: SetPin Flow, CIN         (no counting inputs yet - see the top)
SetPin RainDetectInput, DIN, Pullup
SetPin AbortRun, DIN, Pullup
For i = 0 To NbrValves : SetPin Valve(i), DOUT : Next i

LogMessage "====================== Booting..."
LoadAllConfigVars
ResetFlowMsg = "Historical average&nbsp; &nbsp; <input type=\qsubmit\q name=\qFlowReset\q value=\qRESET\q />"

' startup: check WiFi connection and access to the internet
LogMessage "Checking connection to WiFi"
Timer = 0
Do While MM.Info(IP ADDRESS) = "0.0.0.0"
  If Timer > 5000 Then LogMessage "Timeout - Reboot" : Pause 500 : CPU RESTART
  If Timer < 1000 Then LogMessage "Waiting for WiFi"
  Pause 1000
Loop
LogMessage "Connected.  IP address is " + MM.Info(ip address)
LogMessage "Getting UTC time"
Timer = 0
Do
  If Timer > 5000 Then LogMessage "Timeout - Reboot" : Pause 500 : CPU RESTART
  Pause 1000
  On error skip
  WEB ntp
  If MM.Errno <> 0 And Timer < 2000 Then LogMessage "Waiting for the Internet"
Loop Until MM.Errno = 0

GetDateTime

'PC3: the server port is a statement here, standing in for the
'PC3: WebMite's saved OPTION TCP SERVER PORT 80
WEB TCP SERVER PORT 80
WEB TCP INTERRUPT WebInterrupt

' this is the main processing loop
' it takes approx 20mS for one loop if there is nothing to do
Do
  If WatchDogOn Then WatchDog 20000
  GetDateTime                   ' check if it is time to update the date/time
  ProcessErrors                 ' look for any errors
  MailSend 3                    ' send email if new errors were found
  If Len(EMStatus) And ((((Epoch(NOW) / 3600) / 24) Mod 7) + 4) Mod 7 = 0 Then MailSend 2 ' email weekly status if mail configured

  ' check each schedule and execute if valid - overall this takes about 10ms if there is nothing to do
  NextUpdateIdx  = ((Epoch(NOW) \ (3600 * 24)) + 1) * (3600 * 24) ' the Epoch time for midnight of the next day
  For i = 0 To NbrSched - 1
    ProcessSchedule i
    Pin(RunLed) = 0
  Next i
  NextUpdate = NextUpdateIdx
Loop



''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' routines involved in processing and running a schedule
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' run one schedule
' it will return early is there is a configuration error or if the schedule is not ready to run
Sub ProcessSchedule sched As integer
  Local integer i, x, y, use_week_days
  Local string s
  Static integer LastSecond

  If ManualRun(sched) Then
    ManualRun(sched) = false
  Else
    ' first check that the config for the schedule is valid
    If Enabled(sched) = "" Then ErrConfig(i) = false : Exit Sub
    ErrConfig(sched) = true
    If StartDay(sched) = "" Or StartMonth(sched) = "" Or EndDay(sched) = "" Or EndMonth(sched) = "" Then Exit Sub
    For i = 0 To 6
      If Len(RunDOW(i, sched)) Then Exit For
    Next i
    If i > 6 Then
      If Val(RunDays(sched)) < 1 Then Exit Sub
    Else
      use_week_days = true
    EndIf
    For i = 0 To 7
      If Len(ValveR(i, sched)) Then Exit For
    Next i
    If i > 7 Then Exit Sub
    ErrConfig(sched) = false

    ' check if the current day falls within the from:to range
    s = Right$(Date$, 2)
    x = Epoch(Str$(Val(StartDay(sched)),2,0,"0") + "-" + Str$(Val(StartMonth(sched)),2,0,"0") + "-" + s + " 00:00:00")
    If x > Epoch(NOW) Then
      x = Epoch(Str$(Val(StartDay(sched)),2,0,"0") + "-" + Str$(Val(StartMonth(sched)),2,0,"0") + "-" + Str$(Val(s)-1,2,0,"0") + " 00:00:00")
      If x > Epoch(NOW) Then Exit Sub
    EndIf
    s = Mid$(DateTime$(x), 9, 2)
    y = Epoch(Str$(Val(EndDay(sched)),2,0,"0") + "-" + Str$(Val(EndMonth(sched)),2,0,"0") + "-" + s + " 23:59:59")
    If y < x Then
      y = Epoch(Str$(Val(EndDay(sched)),2,0,"0") + "-" + Str$(Val(EndMonth(sched)),2,0,"0") + "-" + Str$(Val(s)+1,2,0,"0") + " 23:59:59")
      If y < Epoch(NOW) Then Exit Sub
    EndIf
    If Epoch(NOW) < x Or Epoch(NOW) > y Then Exit Sub

    ' check if today is a watering day
    If use_week_days Then
      If RunDOW(((((Epoch(NOW) \ 3600) \ 24) Mod 7) + 4) Mod 7, sched) = "" Then Exit Sub
    Else
      If Epoch(NOW) - x = 0 Then Exit Sub
      If (((Epoch(NOW) - x)\3600)\24) Mod Val(RunDays(sched)) <> 0 Then Exit Sub
    EndIf

    ' check if it is the time of day to start the schedule
    x = (Epoch(NOW) \ (3600 * 24)) * (3600 * 24)                  ' the epoch time for midnight of the current day (seconds)
    y = (Val(RunHours(sched)) * 3600) + Val(RunMints(sched)) * 60 ' number of seconds for the offset

    ' find the start time (as EPOCH time)
    If Val(RunStart(sched)) = 0 Then
      i = x + y              ' after midnight
    ElseIf ErrSunRiseSet = 0 Then
      If Val(RunStart(sched)) = 1 Then
        i = x + SunRise - y    ' before sunrise
      ElseIf Val(RunStart(sched)) = 2 Then
        i = x + SunRise + y    ' after sunrise
      ElseIf Val(RunStart(sched)) = 3 Then
        i = x + SunSet - y     ' before sunset
      ElseIf Val(RunStart(sched)) = 4 Then
        i = x + SunSet + y     ' after sunset
      EndIf
    Else
      ' if cannot get sun rise/set default to 4AM
      i = x + 4 * 3600
    EndIf

    ' this keeps track of the next schedule to run so that when the index.html page is loaded its timer
    ' can be set to automatically refresh when the schedule starts
    If i > Epoch(Now) + 2 And i < NextUpdateIdx Then NextUpdateIdx = i

    ' Get the weather forecast if we are within one hour of running a schedule that depends on the weather.
    ' We get the weather at a random time during this hour to avoid exceeding the 60 calls/min of OWM.
    ' This code depends on the fact that we can call GetForecast repeatedly and it will only access OWM on
    ' the first call of the day.  Note that later we call GetForecast with no conditions in case this
    ' random operation did not make the check.
    If LastSecond <> Epoch(NOW) Then
      LastSecond = Epoch(NOW)
      If Len(Rain(sched)) Or (Val(RunIncrease(sched)) > 0 And Len(TempThresh(sched)) > 0) Then
        If Int(Rnd * 3540) + 60 = i - Epoch(NOW) Then
          GetForecast
        EndIf
      EndIf
    EndIf

    ' check if now is time to run
    If i <> Epoch(NOW) Then Exit Sub

    s = ""
    ' check the rain sensor
    'PC3: a stray ")" removed - MMBasic ignores trailing text after a
    'PC3: complete expression; a compiler does not
    If Len(RainDetect) And Pin(RainDetectInput) = 0 Then
      s = "detected"
    Else
    ' if needed, get the weather and check if rain is forecast.  The forecast may have already been retreived in the code above
      If Len(Rain(sched)) Or (Val(RunIncrease(sched)) > 0 And Len(TempThresh(sched)) > 0) Then GetForecast
      If ErrWeather = false And Len(Rain(sched)) And MaxRain >= RainThreshild Then s = "forecast of " + Str$(MaxRain, 1, 0) + "%"
    EndIf

    If s <> "" Then
      LogMessage "Schedule " + Str$(sched + 1) + " skipped due to rain " + s
      StatusMsg = "<BR/><p style=\qcolor: #00C;\q>Skipping \q" + Title(sched) + "\q due to rain " + s + "</p>"
      NextUpdate = Epoch(Now) + 120
      Pause 2000    ' wait a couple of seconds so that we don't try to start again
      Exit Sub
    EndIf

  EndIf ' end if not a manual run

  LogMessage "Running Schedule " + Str$(sched+1)
  Pin(RunLed) = 1
  LogMessage "Pump/master valve on"
  Pin(Master) = 1
  Pause 500
  For i = 0 To NbrValves
    If ErrWeather = false And Val(RunIncrease(sched)) > 0 And Val(TempThresh(sched)) > 0 Then
      If MaxTemp >= Val(TempThresh(sched)) Then
        RunValve i, sched, Val(ValveR(i, sched)) * (1 + Val(RunIncrease(sched))/100)
      Else
        RunValve i, sched, Val(ValveR(i, sched))
      EndIf
    Else
      RunValve i, sched, Val(ValveR(i, sched))
    EndIf
    If AbortSchedule Then Exit For
  Next i
  Pause 500
  Pin(Master) = 0
  If AbortSchedule Then
      LogMessage "Schedule run aborted"
      StatusMsg = "<p style=\qcolor: #00C;\q>Schedule Stopped</p>"
      AbortSchedule = false
    Else
      StatusMsg = "<p style=\qcolor: #393;\q>Last run was \q" + Title(sched) + "\q finishing on " + FmtDate(Epoch(Now)) + " at " + Left$(Time$, 5) + "</p>"
    EndIf
    CancelB = ""
  LogMessage "Pump/master valve off"
End Sub


' turn on or off a particular valve
Sub RunValve RunV, sched, RunTime
  Local Float ThisI, PeakI, TotI, ThisF, LastAveF
  Local Integer NbrOverloads, SampleIC, SampleF
  If RunTime = 0 Then Exit Sub
  ErrFlow(RunV) = 0
  StatusMsg = "<p style=\qcolor: #00C;\q>Running: \q" + Title(sched) + "\q Valve #" + Str$(RunV + 1)
  CancelB = "<input type=\qsubmit\q name=\qButtonStop\q value=\qSTOP RUNNING SCHEDULE\q />"
  NextUpdate = Epoch(Now) + RunTime * 60
  LogMessage "Valve #" + Str$(RunV+1) + " on for " + Str$(RunTime) + " minutes."
  SampleF = 5000      ' ignore the first 5 seconds of flow measurement
  Timer = 0
  Pin(Valve(RunV)) = 1
  Do While Timer < RunTime * 60000 And AbortSchedule = false
    If WatchDogOn Then WatchDog 20000

    ' measure the flow rate and alarm if abnormal
    'PC3: the flow meter reads AND ZEROES a hardware edge counter
    'PC3: (Pin(Flow) / Pin(Flow) = 0), which needs the counting-input
    'PC3: facility the PC3 kernel does not have yet.  The whole
    'PC3: sampling block is disabled; keep "Flow detection" OFF on
    'PC3: the setup page and none of the surrounding code runs.
    'PC3: If Len(FlowDetect) Then
    'PC3:   If Timer > SampleF Then         ' if it is time to sample
    'PC3:     ThisF = Pin(Flow)
    'PC3:     Pin(Flow) = 0
    'PC3:     If SampleF > 5000 Then        ' ignore the first 5 seconds
    'PC3:       If AveF(RunV) = 0 Then AveF(RunV) = ThisF
    'PC3:       If ThisF > AveF(RunV) + AveF(RunV) * (UpperFtolerance /100) Then ErrFlow(RunV) = 1 : RunTime = 0
    'PC3:       If ThisF < AveF(RunV) - AveF(RunV) * (LowerFtolerance /100) Then ErrFlow(RunV) = 1
    'PC3:       If SampleF = 35000 Then      ' the average is based starting at 5 sec and finishing at 35 seconds
    'PC3:         If ErrFlow(RunV) = 1 Then
    'PC3:           LastAveF = AveF(RunV)
    'PC3:           AveF(RunV) = ((AveF(RunV) * 8) + ThisF)/9   ' if there was an error build the average slowly
    'PC3:         Else
    'PC3:           AveF(RunV) = ((AveF(RunV) * 4) + ThisF)/5
    'PC3:           LastAveF = AveF(RunV)
    'PC3:         EndIf
    'PC3:       EndIf
    'PC3:     EndIf
    'PC3:     SampleF = SampleF + 30000
    'PC3:   EndIf
    'PC3: EndIf

    If Pin(AbortRun) = 0 Then AbortSchedule = true
  Loop

  Pin(Valve(RunV)) = 0
  If Len(FlowDetect) Then
    If ErrFlow(RunV) = 1 Then LogMessage "Abnormal flow rate " + Str$(ThisF) + " detected"
    LogMessage "Valve #" + Str$(RunV+1) + " off.  Average flow: " + Str$(LastAveF)
  Else
    LogMessage "Valve #" + Str$(RunV+1) + " off."
  EndIf
  ProcessErrors
End Sub




'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' the code handling the web server
' it is all driven by interrupts from the web server
' NEVER CALL THESE SUBROUTINES DIRECTLY
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' the main interrupt
Sub WebInterrupt
  Local integer a%, pnbr, i, j, Sw(8)
  Local Integer b(512)
  Local string name, value

  For a% = 1 To MM.Info(MAX CONNECTIONS)
  LongString CLEAR b()
  WEB TCP READ a%, b()
'  longstring print b()
  If LLen(b()) > 0 Then
    If (LInStr(b(),"GET / HTTP") > 0) Or (LInStr(b(),"GET /index.html HTTP") > 0) Then
      SendIndexPage a%
    ElseIf (LInStr(b(),"POST / HTTP") > 0) Or (LInStr(b(),"POST /index.html HTTP") > 0) Then
      i = LInStr(b(), "ButtonRun")
      If i Then
        j = Val(LGetStr$(b(), i + 9, 1))
        ManualRun(j) = true
        StatusMsg = "<p style=\qcolor: #00C;\q>Running: \q" + Title(j) + "\q</p>"
      EndIf
      i = LInStr(b(), "ButtonStop")
      If i Then
        AbortSchedule = true
      EndIf
      NextUpdate = Epoch(Now)
      SendIndexPage a%
    ElseIf LInStr(b(),"GET /setup.html HTTP") > 0 Then 'PC3: stray ")" removed
      SendSetupPage a%
    ElseIf LInStr(b(),"POST /setup.html HTTP") > 0 And LInStr(b(), "SubmitForm=") > 0 Then
      ProcessSetupData b()
      LogMessage "Running location test"
      GetLatLong
      If OWMLong <> 9999 Or DisableLocationWarning Then GetDateTime 1
      SaveAllConfigVars
      EmailTestMsg = ""
      SavedMsg = "<div align=\qcenter\q><p style=\qcolor: #393; font-weight: bold;\q>Settings Saved OK</p></div>"
      SendSetupPage a%
      SavedMsg = ""
    ElseIf LInStr(b(),"POST /setup.html HTTP") > 0 And LInStr(b(), "OWMTest=") > 0 Then
      ProcessSetupData b()
      LogMessage "Running location test"
      GetLatLong 1
      If OWMLong <> 9999 Or DisableLocationWarning Then GetDateTime 1
      SendSetupPage a%
    ElseIf LInStr(b(),"POST /setup.html HTTP") > 0 And LInStr(b(), "FlowReset=") > 0 Then
      ProcessSetupData b()
      LogMessage "Resetting flow history"
      For i = 0 To NbrValves : AveF(i) = 0 : Next i
      ResetFlowMsg = "<align=\qcenter;\q style=\qcolor: #393;\q>Average Reset</>"
      SendSetupPage a%
      ResetFlowMsg = "Historical average&nbsp; &nbsp; <input type=\qsubmit\q name=\qFlowReset\q value=\qRESET\q />"
    ElseIf LInStr(b(),"POST /setup.html HTTP") > 0 And LInStr(b(), "EmailTest=") > 0 Then
      ProcessSetupData b()
      LogMessage "Running email test"
      MailSend  1
      SendSetupPage a%
    ElseIf (LInStr(b(),"GET /config") > 0) And (LInStr(b(),".html HTTP") > 0) Then 'PC3: "(" restored
      pnbr = Val(LGetStr$(b(), LInStr(b(),"GET /config") + 11, 1))
      SendConfigPage pnbr, a%
    ElseIf (LInStr(b(),"POST /config") > 0) And (LInStr(b(),".html HTTP") > 0) Then 'PC3: "(" restored
      pnbr = Val(LGetStr$(b(), LInStr(b(),"POST /config") + 12, 1))
      i = LInStr(b(), "Title=")
      j = LInStr(b(), "SubmitForm=")
      If i = 0 Or j = 0 Or j <= i Then WEB TRANSMIT CODE a%, 404 : Continue For
      LogMessage "Processing configuration changes"
      ProcessConfigData b(), pnbr
      SaveAllConfigVars
      SavedMsg = "<div align=\qcenter\q><p style=\qcolor: #393; font-weight: bold;\q>Configuration Saved OK</p></div>"
      SendConfigPage pnbr, a%
      SavedMsg = ""
    Else
      LogMessage "Sending error 404 page not found"
      'PC3: ConnData() is in no WebMite reference we hold; the request
      'PC3: text is already in b() at this point, so ask b()
      If LInStr(b(),MM.Info(ip address)) > 0 Then 'Is the WebMite address included in the data?
        WEB TRANSMIT CODE a%, 404
      Else
        WEB tcp close a%
      EndIf
    EndIf
  EndIf
  Next a%
End Sub


' prepare and send index.html
Sub SendIndexPage a%
  Local integer i, x
  Local string Status(NbrSched), RunNow(NbrSched), ConfigB(NbrSched), SetupB
  Local float timeup=MM.Info(uptime)
  For i = 0 To NbrSched - 1
    If Enabled(i) = "" Then
      Status(i) = "Disabled"
    Else
      If Len(StartDay(i)) <> 0 And Len(StartMonth(i)) <> 0 And Len(EndDay(i)) <> 0 And Len(EndMonth(i)) <> 0 Then
        Status(i) = StartDay(i)+ "/" + StartMonth(i) + " to " + EndDay(i) + "/" + EndMonth(i)
      Else
        Status(i) = "Config Error"
      EndIf
      For x = 0 To 6
        If Len(RunDOW(x, i)) Then Exit For
      Next x
      If x > 6 And Val(RunDays(i)) = 0 Then Status(i) = "Config Error"
      For x = 0 To NbrValves
        If Val(ValveR(x, i)) > 0 Then Exit For
      Next x
      If x > NbrValves Then Status(i) = "Config Error"
    EndIf
    ErrConfig(i) = (Status(i) = "Config Error")
    If Instr(CancelB, "STOP RUNNING") = 0 Then
      ConfigB(i) = "<input type=\qbutton\q onclick=\qwindow.location.href='config" + Str$(i) + ".html'; \q value=\qCONFIGURE\q />"
      SetupB = "<input type=\qbutton\q onclick=\qwindow.location.href='setup.html';\q value=\q GENERAL SETTINGS \q />"
    Else
'      ConfigB(i) = "<input type=\qbutton\q onclick=\qwindow.location.href='index.html';\q value=\qCONFIGURE\q />"
'      SetupB = "<input type=\qbutton\q onclick=\qwindow.location.href='index.html';\q value=\q GENERAL SETTINGS \q />"
      ConfigB(i) = "<input type=\qbutton\q onclick=\qwindow.location.href='index.html';\q  value=\q                     \q />"
      SetupB = ""
    EndIf
    RunNow(i) = ""
    For x = 0 To NbrValves
      If Instr(CancelB, "STOP RUNNING") = 0 Then
        If Val(ValveR(x, i)) > 0 Then RunNow(i) = "<input type=\qsubmit\q name=\qButtonRun" + Str$(i) + "\qvalue=\qRUN NOW\q />"
      Else
        If Val(ValveR(x, i)) > 0 Then RunNow(i) = "<input type=\qbutton\q onclick=\qwindow.location.href='index.html';\q value=\q                  \q />"
      EndIf
    Next x
  Next i

  EmailTestMsg = "" : LocationTestMsg = "" : LocationDataMsg = ""
  RefreshTime = (NextUpdate - Epoch(Now) + 2) * 1000
  WEB TRANSMIT PAGE a%, "index.html" 'PC3: the page lives next to the program; a bare leading / is an absolute path here
  LogMessage "Sending default page. Auto refresh set to " + Str$(RefreshTime/1000) + " seconds."
End Sub


' prepare and send the setup.html page
Sub SendSetupPage a%
  LogMessage "Sending setup page"
  WEB TRANSMIT PAGE a%, "setup.html" 'PC3: as index.html
End Sub


' process the POST response from the user's browser to setup.html
Sub ProcessSetupData b() As integer
  Local Integer i, x
  Local String name, value

  LogMessage "Processing setup changes"
'  longstring print b()
  EMW = "" : EMTime = "" : EMFlow = "" : EMStatus = "" : FlowDetect = "" : RainDetect = ""
  i = LInStr(b(), "OWMCity=")
  Do
    GetNextPair b(), i, name, value
    If i = 0 Then
      If EmailFrom = "" Or EmailTo = "" Then EMW = "" : EMTime = "" : : EMFlow = "" : EMStatus = ""
      Exit Sub
    EndIf
'    ? name + ":" + value,;
    Select Case name
      Case "OWMCity"
        For x = 1 To Len(value)
          If Mid$(value, x, 1) = "+" Then
            MID$(value, x, 1) = " "
          EndIf
        Next x
        OWMCity = value
      Case "OWMCountry"
        OWMCountry = value
      Case "FlowDetect"
        If value = "on" Then FlowDetect = "checked"
      Case "RainDetect"
        If value = "on" Then RainDetect = "checked"
      Case "Smtp2GoUser"
        Smtp2GoUser = value
      Case "Smtp2GoPasswd"
        Smtp2GoPasswd = value
      Case "SGKey"
        SGKey = value
      Case "EmailFrom"
        EmailFrom = value
      Case "EmailTo"
        EmailTo = value
      Case "EMTime"
        If value = "on" Then EMTime = "checked"
      Case "EMW"
        If value = "on" Then EMW = "checked"
      Case "EMFlow"
        If value = "on" Then EMFlow = "checked"
      Case "EMStatus"
        If value = "on" Then EMStatus = "checked"
      Case "OWMTest", "EmailTest", "SubmitForm", "FlowReset"
        ' ignore
      Case Else
        LogMessage "No variable: "+name
    End Select
  Loop
End Sub


' prepare and send the configx.html page
Sub SendConfigPage pnbr, a%
  Local integer i, x
  Local string ErrorMsg1, ErrorMsg2, ErrorMsg3, Sw(8)

  ErrConfig(pnbr) = false
  If Len(RunStart(pnbr)) Then Sw(Val(Right$(RunStart(pnbr), 1))) = "selected"
  If Len(Enabled(pnbr)) Then
    If StartDay(pnbr) = "" Or StartMonth(pnbr) = "" Or EndDay(pnbr) = "" Or EndMonth(pnbr) = "" Then 'PC3: stray ")" removed
      ErrorMsg1 = "<div align=\qcenter\q><p style=\qcolor: #F00; font-weight: bold;\q>Active Period not set</p></div>"
      ErrConfig(pnbr) = true
    EndIf
    For i = 0 To 6
      If Len(RunDOW(i, pnbr)) Then Exit For
    Next i
    If i <= 6 Then
      RunDays(pnbr) = ""
    ElseIf Val(RunDays(pnbr)) = 0 Then
      ErrorMsg2 = "<div align=\qcenter\q><p style=\qcolor: #F00; font-weight: bold;\q>Watering Days not set</p></div>"
      ErrConfig(pnbr) = true
    EndIf
    For i = 0 To NbrValves
      If Val(ValveR(i, pnbr)) > x Then Exit For
    Next i
    If i > NbrValves Then
      ErrorMsg3 = "<div align=\qcenter\q><p style=\qcolor: #F00; font-weight: bold;\q>Valve Run Time not set</p></div>"
      ErrConfig(pnbr) = true
    EndIf
  EndIf
  LogMessage "Sending configuration page"
  WEB TRANSMIT PAGE a%, "config.html" 'PC3: as index.html
End Sub



' process the POST response from the user's browser to configx.html
Sub ProcessConfigData b() As integer, pnbr
  Local Integer i, x
  Local String name, value
  Local String  Sw(5) Length 16

  Enabled(pnbr) = "" : Rain(pnbr) = ""
  For x = 0 To 6 : RunDOW(x, pnbr) = "" : Next x
  i = LInStr(b(), "Title=")
  Do
    GetNextPair b(), i, name, value
    If i = 0 Then
      Exit Sub
    EndIf
'    ? name + ":" + value,;
    If value = "on" Then value = "checked"
    For x = 1 To Len(value)
      If Mid$(value, x, 1) = "+" Then
        MID$(value, x, 1) = " "
      EndIf
    Next x
    Select Case name
      Case "Title"
        Title(pnbr) = value
      Case "Enabled"
        Enabled(pnbr) = value
      Case "StartDay"
        StartDay(pnbr) = CheckValue(value, 1, 31)
      Case "StartMonth"
        StartMonth(pnbr) = CheckValue(value, 1, 12)
      Case "EndDay"
        EndDay(pnbr) = CheckValue(value, 1, 31)
      Case "EndMonth"
        EndMonth(pnbr) = CheckValue(value, 1, 12)
      Case "RunDOW0"
        RunDOW(0, pnbr) = value
      Case "RunDOW1"
        RunDOW(1, pnbr) = value
      Case "RunDOW2"
        RunDOW(2, pnbr) = value
      Case "RunDOW3"
        RunDOW(3, pnbr) = value
      Case "RunDOW4"
        RunDOW(4, pnbr) = value
      Case "RunDOW5"
        RunDOW(5, pnbr) = value
      Case "RunDOW6"
        RunDOW(6, pnbr) = value
      Case "RunDays"
        RunDays(pnbr) = CheckValue(value, 1, 365)
      Case "RunHours"
        RunHours(pnbr) = CheckValue(value, 1, 24)
        If RunHours(pnbr) = "" Then RunHours(pnbr) = "0"
      Case "RunMints"
        RunMints(pnbr) = CheckValue(value, 1, 59)
        If RunMints(pnbr) = "" Then RunMints(pnbr) = "0"
      Case "RunStart"
        RunStart(pnbr) = Right$(value, 1)
      Case "ValveR0"
        ValveR(0, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR1"
        ValveR(1, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR2"
        ValveR(2, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR3"
        ValveR(3, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR4"
        ValveR(4, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR5"
        ValveR(5, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR6"
        ValveR(6, pnbr) = CheckValue(value, 1, 999)
      Case "ValveR7"
        ValveR(7, pnbr) = CheckValue(value, 1, 999)
      Case "Rain"
        Rain(pnbr) = value
      Case "RunIncrease"
        RunIncrease(pnbr) = CheckValue(value, 1, 300)
      Case "TempThresh"
        TempThresh(pnbr) = CheckValue(value, 1, 99)
      Case "SubmitForm"
        'ignore
      Case Else
        LogMessage "No variable: "+name
    End Select
  Loop
End Sub



' used to verify an entry when processing POST data
Function CheckValue(value As string, min As integer, max As integer) As string
  Local Integer x

  If Len(value) Then
    x = Val(value)
    If x >= min And x <= max Then CheckValue = Str$(x) Else CheckValue = ""
  Else
    CheckValue = ""
  EndIf
End Function


' used to find and return the next name/value pair when processing POST data
Sub GetNextPair b%(), i As integer, n$, value$
  Local integer x, y, z, j

  x = LInStr(b%(), "=", i)
  If x = 0 Then i = 0 : Exit Sub
  y = LInStr(b%(), "&", i)
  n$ = LGetStr$(b%(), i, x - i)
  If y = 0 Then y = LLen(b%()) + 1
  If y - x - 1 <= 0 Then
    value$ = ""
  Else
    value$ = LGetStr$(b%(), x + 1, y - x - 1)
    j = 1
    Do
      z = Instr(j, value$, "%")
      If z = 0 Then Exit
      value$ = Left$(value$, z - 1) + Chr$(Val("&H" + Mid$(value$, z + 1, 2))) + Mid$(value$, z + 3)
      If MM.Errno Then Exit
      j = z + 1
    Loop
  EndIf
  i = y + 1
End Sub



'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' these two routines save and load the config and setup values
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' save all the config and setup values to the file A:/settings.dat
Sub SaveAllConfigVars
  Local integer i, j

  LogMessage "Saving config and settings data to A:/settings.dat"
  Open "A:/settings.dat" For output As #1
  Print #1, Ver
  For i = 0 To NbrSched
    Print #1, Title(i)
    Print #1, Enabled(i)
    Print #1, StartDay(i)
    Print #1, StartMonth(i)
    Print #1, EndDay(i)
    Print #1, EndMonth(i)
    For j = 0 To 7
      Print #1, RunDOW(j, i)
    Next j
    Print #1, RunDays(i)
    Print #1, RunHours(i)
    Print #1, RunMints(i)
    Print #1, RunStart(i)
    For j = 0 To NbrValves
      Print #1, ValveR(j,i)
    Next j
    Print #1, Rain(i)
    Print #1, RunIncrease(i)
    Print #1, TempThresh(i)
  Next i
  Print #1, OWMlat
  Print #1, OWMlong
  Print #1, TimeZone
  Print #1, OWMCity
  Print #1, OWMCountry
  Print #1, FlowDetect
  Print #1, FlowDetect
  Print #1, RainDetect
  Print #1, Smtp2GoUser
  Print #1, Smtp2GoPasswd
  Print #1, SGKey
  Print #1, EmailFrom
  Print #1, EmailTo
  Print #1, EMTime
  Print #1, EMW
  Print #1, EMFlow
  Print #1, EMFlow
  Print #1, EMStatus
  Close #1
End Sub



' load all the config and setup values from the file A:/settings.dat
Sub LoadAllConfigVars
  Local integer i, j
  Local String s, version

  On error skip
  Open "A:/settings.dat" For input As #1
  If MM.Errno Then
    For i = 0 To NbrSched
      Title(i) = "Schedule " + Str$(i+1)
    Next i
    Exit Sub
  EndIf
  LogMessage "Loading config and settings data from A:/settings.dat"
  Line Input #1, version   ' this is the version number of the config file
  For i = 0 To NbrSched
    Line Input #1, Title(i)
    Line Input #1, Enabled(i)
    Line Input #1, StartDay(i)
    Line Input #1, StartMonth(i)
    Line Input #1, EndDay(i)
    Line Input #1, EndMonth(i)
    For j = 0 To 7
      Line Input #1, RunDOW(j, i)
    Next j
    Line Input #1, RunDays(i)
    Line Input #1, RunHours(i)
    Line Input #1, RunMints(i)
    Line Input #1, RunStart(i)
    For j = 0 To NbrValves
      Line Input #1, ValveR(j,i)
    Next j
    Line Input #1, Rain(i)
    Line Input #1, RunIncrease(i)
    Line Input #1, TempThresh(i)
  Next i
  Line Input #1, s : OWMlat = Val(s)
  Line Input #1, s : OWMlong = Val(s)
  Line Input #1, s : TimeZone = Val(s)
  Line Input #1, OWMCity
  Line Input #1, OWMCountry
  Line Input #1, FlowDetect
  Line Input #1, FlowDetect
  Line Input #1, RainDetect
  If Val(version) >= 1.3 Then
    Line Input #1, Smtp2GoUser
    Line Input #1, Smtp2GoPasswd
  EndIf
  Line Input #1, SGKey
  Line Input #1, EmailFrom
  Line Input #1, EmailTo
  Line Input #1, EMTime
  Line Input #1, EMW
  Line Input #1, EMFlow
  Line Input #1, EMFlow
  Line Input #1, EMStatus
  Close #1
End Sub


'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' these routines deal with Open Weather Map for timezeone, sunrise, sunset and weather forecast
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' use the GeocodingAPI to get the GPS coordinates of the city
' this is only run if the user clicked on TEST or saved the general setup page
Sub GetLatLong Test As integer
  Local Integer b(4096/8), i
  Local String C, S, cu

  LocationTestMsg = "<div align=\qcenter\q><p style=\qcolor: #F00; font-weight: bold;\q>Error accessing location data<BR />Location not set.</p></div>"

  LogMessage "Get the latitude and longtitude of a city"
  On ERROR SKIP 3
  WEB OPEN TCP CLIENT "api.openweathermap.org", 80
  WEB TCP CLIENT REQUEST "GET /geo/1.0/direct?q="+OWMCity+","+OWMCountry+"&limit=1&appid="+OWMKey+Chr$(10)+Chr$(13), b()
  WEB CLOSE TCP CLIENT
  If MM.Errno <> 0 Then Exit Sub
  ' longstring print b()
  LongString mid b(), b(),2
  On error skip 5
  C = Json$(b(), "name")
  cu = Json$(b(), "country")
  S = Json$(b(), "state")
  OWMlat = Val(Json$(b(), "lat"))
  OWMlong = Val(Json$(b(), "lon"))
  If MM.Errno <> 0 Or C = "" Or cu = "" Or OWMlat + OWMlong = 0 Then OWMlat = 9999 : LogMessage "Could not resolve location" : Exit Sub
  LocationTestMsg = "" : LocationDataMsg = ""
  If Test Then
    LocationTestMsg = "<div align=\qcenter\q><p style=\qcolor: #393; font-weight: bold;\q>Success.<BR />"
    LocationTestMsg = LocationTestMsg + "Found " + C + ", " + S + ", " + cu + "</p></div>"
    GetDateTime true
    If ErrNTP = true Then
      LocationDataMsg = "<div align=\qcenter\q><p style=\qcolor:red; font-weight: bold;\q>Cannot get the time</p></div>"
    Else
      GetForecast 1
      If ErrWeather = true Then
        LocationDataMsg = "<div align=\qcenter\q><p style=\qcolor:red; font-weight: bold;\q>Cannot get the weather</p></div>"
      Else
        LocationDataMsg = "<div align=\qcenter\q><p style=\qcolor:#00f; font-size:14px;\q>" + DateTime$(Now) + "<BR />Timezone = " + Str$(TimeZone) + "&nbsp;&nbsp;&nbsp;Sunrise = "
        LocationDataMsg = LocationDataMsg + Mid$(DateTime$(SunRise), 12, 5) + "&nbsp;&nbsp;&nbsp;Sunset = " + Mid$(DateTime$(SunSet), 12, 5)
        LocationDataMsg = LocationDataMsg + "&nbsp;&nbsp;&nbsp;Max = " + Str$(MaxTemp, 1, 0) + "&deg;C&nbsp;&nbsp;&nbsp;Rain = " + Str$(MaxRain) + "%</p></div>"
      EndIf
    EndIf
  Else
    LogMessage "Found " + C + ", " + S + ", " + cu + "\r\n    latitude = " + Str$(OWMlat) + "  longtitude = " + Str$(OWMlong)
  EndIf
End Sub



' get the date, time and timezone (which might change due to daylight saving)
' this is repeatedly called from the main program loop but it only accesses
' Open Weather Map on average once a day (at random times) to avoid exceeding
' the access limits imposed by Open Weather Map on the free account
Sub GetDateTime Test As integer
  Static Integer NextTimeSync

  If Test = 0 And Epoch(Now) < NextTimeSync Then Exit Sub

  ErrNTP = true

  If OWMlat = 9999 Then
    ' if the location not set at least try to get the time using the DefaultTimeZone
    ErrTimezone = true : ErrSunRiseSet = true
    On Error Skip 1
    WEB NTP DefaultTimeZone
    If MM.Errno Then
      Pause 10000
      On Error Skip 1
      WEB NTP DefaultTimeZone
      If MM.Errno Then
        LogMessage "Error getting the NTP time"
        ErrNTP = true
      EndIf
    EndIf
    ErrNTP = false
    LogMessage "Using timezone" + Str$(DefaultTimeZone) + " current date/time = " + DateTime$(Now)
    NextTimeSync = Epoch(Now) + 24 * 3600 +(Rnd * (24 * 3600))  ' set a random time in the next 24 to 48 hours to get the time again
    Exit Sub
  Else
    ' we have a valid location
    GetSunRiseSet      ' this also gets the timezone
  EndIf

  NextTimeSync = Epoch(Now) + 24 * 3600 +(Rnd * (24 * 3600))  ' set a random time in the next 24 to 48 hours to get the time again
End Sub 'PC3: was "Exit Sub" - the sub had no End Sub; MMBasic did not mind, a compiler does



' get the timezone (in seconds), sunrise, sunset and the current time (in Epoch time)
Sub GetSunRiseSet
  Local Integer b(4096/8)
  Local Float i

  LogMessage "Get the timezone (incl DST), sunrise and sunset"
  ErrTimezone = true : ErrSunRiseSet = true : ErrNTP = true

  On ERROR SKIP 3
  WEB OPEN TCP CLIENT "api.openweathermap.org", 80
  WEB TCP CLIENT REQUEST "GET /data/2.5/weather?lat="+Str$(OWMlat)+"&lon="+Str$(OWMlong)+"&appid="+OWMKey+Chr$(10)+Chr$(13), b()
  WEB CLOSE TCP CLIENT
  If MM.Errno Then Exit Sub
  On ERROR SKIP 1
  i = Val(Json$(b(), "timezone"))
  If MM.Errno Or Val(Json$(b(), "sys.sunrise")) = 0 Or i < -14 * 3600 Or i > 14 * 3600 Then
    TimeZone = DefaultTimeZone
    LogMessage "Error getting timezone.  Using timezone = " + Str$(DefaultTimeZone)
  Else
    TimeZone = i/3600
    ErrTimezone = false
  EndIf

  ' now get the time
  ErrNTP = false
  On ERROR SKIP 1
  WEB NTP TimeZone
  If MM.Errno Then          ' if error
    Pause 10000             ' wait 10 seconds
    On error skip
    WEB NTP TimeZone        ' and try again
    If MM.Errno Then LogMessage "Error getting the NTP time" : Exit Sub
  EndIf

  If ErrTimezone = true Then Exit Sub

  On Error Skip 2
  SunRise = Val(Json$(b(), "sys.sunrise")) + TimeZone * 3600
  SunSet = Val(Json$(b(), "sys.sunset")) + TimeZone * 3600
  If MM.Errno Or SunRise = 0 Then Exit Sub
  LogMessage "Timezone = " + Str$(TimeZone) + ", sunrise = " + Mid$(DateTime$(SunRise), 12, 5) + ", sunset = " + Mid$(DateTime$(SunSet), 12, 5)

  ' convert sun rise/set to seconds after midnight
  SunRise = Epoch("01-01-1970 " + Right$(DateTime$(SunRise), 8))
  SunSet = Epoch("01-01-1970 " + Right$(DateTime$(SunSet), 8))
  ErrSunRiseSet = false
End Sub


' get the max temperature and probability of rain
Sub GetForecast Test As integer
  Local Integer b(4096/8), i, j
  Local float v
  Static String GotForecast

  If Test = 0 And GotForecast = Left$(DateTime$(Now), 10) Then Exit Sub
  GotForecast = Left$(DateTime$(Now), 10)

  ErrWeather = true
  If OWMlat = 9999 Then Exit Sub

  LogMessage "Get the weather forecast"
  MaxTemp = -9999 : MaxRain = 0
  On ERROR SKIP
  WEB OPEN TCP CLIENT "api.openweathermap.org", 80
  If MM.Errno <> 0 Then Exit Sub
  On ERROR SKIP
  WEB TCP CLIENT REQUEST "GET /data/2.5/forecast?lat="+Str$(OWMlat)+"&lon="+Str$(OWMlong)+"&appid="+OWMKey+Chr$(10)+Chr$(13), b()
  If MM.Errno <> 0 Then Exit Sub
  On ERROR SKIP
  WEB CLOSE TCP CLIENT
  If MM.Errno <> 0 Then Exit Sub
  ' LongString print b()
  i = LInStr(b(), "\qtemp_max\q:")
  If i = 0 Then Exit Sub
  j = 0
  Do While i > 0 And j < 8
    v = Val(LGetStr$(b(), i + 11, 6)) - 273
    If v > MaxTemp Then MaxTemp = v
    i = LInStr(b(), "\qtemp_max\q:", i + 11)
    j = j + 1
  Loop
  i = LInStr(b(), "\qpop\q:")
  If i = 0 Then Exit Sub
  j = 0
  Do While i > 0 And j < 8
    v = Val(LGetStr$(b(), i + 6, 4)) * 100
    If v > MaxRain Then MaxRain = v
    i = LInStr(b(), "\qpop\q:", i + 6)
    j = j + 1
  Loop
  ErrWeather = false
  LogMessage "Forecast max temp = " + Str$(MaxTemp) + "C, rain = " + Str$(MaxRain) + "%"
End Sub


''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' subroutine used to send emails
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' send an email
Sub MailSend Mode As integer
  Const cr = Chr$(13)+Chr$(10)
  Local integer b(512), SendMsg = false
  Static integer LastMsgSize
  Local String body, txt
  Static string DateLastEmail

  If Mode <> 1 Then
    If Len(EmailStatusMsg) = 0 Then Exit Sub
    If ErrEmail = false And DateLastEmail = Date$ And Len(EmailStatusMsg) = LastMsgSize Then Exit Sub
  EndIf
  If Mode = 1 Then
    ' we just need to send a test email
    txt = "Test email from the reticulation controller"
    SendMsg = true
  ElseIf Mode = 2 Then
    ' weekly status report
    If EmailFrom = "" Or EmailTo = "" Then Exit Sub
    txt = "Current status as at " + DateTime$(Now) + ":\r\n"
    If EmailStatusMsg = "" Then
      txt = txt + "No errors or faults."
    Else
      txt = txt + EmailStatusMsg
    EndIf
    SendMsg = true
  Else
    ' normal email
    If EmailFrom = "" Or EmailTo = "" Then Exit Sub
    txt = EmailStatusMsg
  EndIf

  If Len(EMTime) <> 0 And ErrNTP Then SendMsg = true
  If Len(EMW) <> 0 And DisableLocationWarning = 0 And (OWMLat = 9999 Or ErrTimezone Or ErrSunRiseSet Or ErrWeather) Then SendMsg = true
  If Len(EMFlow) <> 0 And Instr(EmailStatusMsg, "flow") <> 0 Then SendMsg = true

  If SendMsg = false Then Exit Sub

  LastMsgSize = Len(EmailStatusMsg)
  body = "From: " + EmailFrom + cr + "To: " + EmailTo + cr
  body = body + "Subject: Reticulation Controller" + cr + cr
  body = body + txt + cr + "." + cr
  LogMessage "Sending email"
  'PC3: Gmail replaces both SendGrid and SMTP2GO - SendGrid is
  'PC3: unusable and SMTP2GO's free tier is gone.  Implicit TLS on
  'PC3: 465 with AUTH LOGIN and an App Password (2-Step Verification
  'PC3: on the Google account, then myaccount.google.com/apppasswords
  'PC3: - 16 characters, spaces removed).  On the setup page the
  'PC3: "username" field is the full Gmail address and the "password"
  'PC3: field is the App Password; the API-key field is unused.  The
  'PC3: 220 greeting lands during/after OPEN and the first REQUEST
  'PC3: discards it, so EHLO gets EHLO's answer - the same
  'PC3: drop-before-write both firmwares share.
  On Error Skip
  WEB TLS CA "/etc/ca.pem"
  On Error Skip
  WEB OPEN TLS CLIENT "smtp.gmail.com", 465, 20000
  Pause 300
  On Error Skip
  WEB TCP CLIENT REQUEST "EHLO retic" + cr, b()
  On Error Skip
  WEB TCP CLIENT REQUEST "AUTH LOGIN" + cr, b()
  On Error Skip
  WEB TCP CLIENT REQUEST base64Encode(Smtp2GoUser) + cr, b()
  On Error Skip
  WEB TCP CLIENT REQUEST base64Encode(Smtp2GoPasswd) + cr, b()
  If MM.Errno = 0 Then
    Pause 300
    On Error Skip
    WEB TCP CLIENT REQUEST "MAIL FROM:<" + EmailFrom + ">" + cr, b() 'PC3: angle brackets, the form proven against Gmail
    On Error Skip
    WEB TCP CLIENT REQUEST "RCPT TO:<" + EmailTo + ">" + cr, b()
    On Error Skip
    WEB TCP CLIENT REQUEST "DATA" + cr, b()
    Pause 300
    On Error Skip
    WEB TCP CLIENT REQUEST body, b()
    Pause 500
    On Error Skip
    WEB CLOSE TCP CLIENT
  EndIf

  'PC3: Gmail's acceptance is "250 2.0.0 OK ..." - neither of the old
  'PC3: providers' spellings matches it
  If LInStr(b(), "250 2.0.0") = 0 Then
    ErrEmail = true
    FaultDetected = true
    LogMessage "Error sending email"
    EmailTestMsg = "<div align=\qcenter\q><p style=\qcolor: #F00; font-weight: bold;\q>Error sending email.</p></div>"
  Else
    ErrEmail = false
    LogMessage "Email sent"
    EmailTestMsg = "<div align=\qcenter\q><p style=\qcolor: #393; font-weight: bold;\q>Success.  Email sent.</p></div>"
  EndIf

If Mode <> 1 Then EmailTestMsg = ""
DateLastEmail = Date$
End Sub



''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''
' utility subroutines
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' step through the error flags and build a string to display these on a web page and in an email
Sub ProcessErrors
  Local integer i
  Local string s = ""

  ErrorStatusMsg = ""
  EmailStatusMsg = ""
  For i = 0 To NbrSched - 1
    If ErrConfig(i) = true Then
      If s <> "" Then s = s + " and"
      s = s + Str$(i + 1, 3)
    EndIf
  Next i
  If Len(s) Then
    ErrorStatusMsg = "Configuration error in schedule"
    If Len(s) > 3 Then ErrorStatusMsg = ErrorStatusMsg + "s"
    ErrorStatusMsg = ErrorStatusMsg + " " + s
    EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
    ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
  EndIf
  If ErrNTP Then
    ErrorStatusMsg = ErrorStatusMsg + "Error in getting network time (check Internet)"
    EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
    ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
  EndIf
  If DisableLocationWarning = false Then
    If OWMLat = 9999 Then
      ErrorStatusMsg = ErrorStatusMsg + "Location not set"
      EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
      ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
    EndIf
    If ErrTimezone Then
      ErrorStatusMsg = ErrorStatusMsg + "Cannot get the timezone"
      EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
      ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
    EndIf
    If ErrSunRiseSet Then
      ErrorStatusMsg = ErrorStatusMsg + "Cannot get the sun rise/set"
      EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
      ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
    EndIf
    If ErrWeather Then
      ErrorStatusMsg = ErrorStatusMsg + "Cannot get the weather forecast"
      EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
      ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
    EndIf
  EndIf

  If ErrEmail Then ErrorStatusMsg = ErrorStatusMsg + "Error: Cannot send emails\r\n"

  s = ""
  For i = 0 To NbrValves - 1
    If ErrFlow(i) Then s = s + Str$(i + 1, 3)
  Next i
  If Len(s) Then
    ErrorStatusMsg = "Detected abnormal water flow on valve"
    If Len(s) > 3 Then ErrorStatusMsg = ErrorStatusMsg + "s"
    ErrorStatusMsg = ErrorStatusMsg + " " + s
    EmailStatusMsg = ErrorStatusMsg +  ".\r\n"
    ErrorStatusMsg = ErrorStatusMsg +  ".<BR />"
  EndIf
  FaultDetected = (ErrorStatusMsg <> "")
End Sub


' this is called by a SetTick interrupt and will flash the LED if an error has occures
Sub FlashLED
  Static LedState
  If FaultDetected = false Then LedState = false
  LedState = Not LedState
  Pin(LED) = LedState
End Sub


' convert a string into a Base64 encoded string
Function base64Encode(si As string) As string
'  Local string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
'  Local integer i, p, n, pad
'
'  For p = 1 To Len(si) Step 3
'    n = Asc(Mid$(si, p, 1)) << 8
'    If p + 1 <= Len(si) Then n = n Or Asc(Mid$(si, p + 1, 1)) Else pad = pad + 1
'    n = (n << 8)
'    If p + 2 <= Len(si) Then n = n Or Asc(Mid$(si, p + 2, 1)) Else pad = pad + 1
'    For i = 3 To 0 Step -1
'      base64Encode = base64Encode + Mid$(b64, ((n >> (i * 6)) And &b111111) + 1, 1)
'    Next i
'  Next p
'  base64Encode = Left$(base64Encode, Len(base64Encode) - pad) + Left$("==", pad)
Local integer iiii=Math(base64 encode si,base64Encode)
End Function


' return the date nicely formatted with the month as "dd mmm"
Function FmtDate(ep As Integer) As String
  Local string mth(12) = ("", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")

  FmtDate = Left$(DateTime$(ep), 2) + " " + mth(Val(Mid$(DateTime$(ep), 4, 2)))
End Function


Sub LogMessage msg$
  Local Integer size
  If LogToConsole Then Print DateTime$(Now) "  " msg$
  If LogToFile Then
    Open "log_current.txt" For Append As #9
    Print #9, DateTime$(Now) "  " msg$
    size = Lof(#9)
    Close #9
    If size > MM.Info(DISK SIZE)/3 Then Copy "log_current.txt" To "log_old.txt"
  EndIf
End Sub
