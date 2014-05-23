#mod_vstatus

### statistic tool for managing vhosts in apache2
mod vstatus is a tool to collect apache requests summarized by vhost and status-code.

##Highlights
  - highly configurable
  - different output styles (csv,google charts, html)
  - filter by status code


##example output

###csv

    Time,Host,Total,1xx,2xx,3xx,4xx,5xx,100,101,200,201,202,203,204,205,206,300,301,302,303,304,305,306,307,400,401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,500,501,502,503,504,505
    1394439580,Total,4497,0,3688,646,127,36,0,0,3182,0,0,0,0,0,506,0,531,3,2,110,0,0,0,1,97,0,0,10,0,0,0,19,0,0,0,0,0,0,0,0,0,36,0,0,0,0,0
    1394439580,<UNNAMED>,122,0,103,0,19,0,0,0,103,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,19,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439580,<VHOST1>,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439580,<VHOST2>,347,0,343,4,0,0,0,0,343,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439580,<VHOST3>,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439565,Total,4497,0,3688,646,127,36,0,0,3182,0,0,0,0,0,506,0,531,3,2,110,0,0,0,1,97,0,0,10,0,0,0,19,0,0,0,0,0,0,0,0,0,36,0,0,0,0,0
    1394439565,<UNNAMED>,122,0,103,0,19,0,0,0,103,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,19,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439565,<VHOST1>,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439565,<VHOST2>,347,0,343,4,0,0,0,0,343,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    1394439565,<VHOST3>,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0


##Quick-start

    checkout sourcecode
    make
    make install


add the following config directives to the apache config (e.g. /etc/apache2/http.conf)

    <Location /vstatus>
    	SetHandler vstatus
    	Order deny,allow
    	Allow from 127.0.0.0/255.0.0.0
    	Allow From 192.168.20.0/24
    </Location>
    
    vstatusHistSize 120				#log 120 values per vhost
    vstatusGranularity 1			# 1 second per log entry
	# time monitored = vstatusGranularity*vstatusHistSize = 120 seconds
    
    #vstatusFilter  <NAME> 0 1 2 3 4 5	# summarize error-codes (0 = all errors, 1 = 1xx ...)
    #vstatusFormat  <NAME> <format>		# format=<csv,json,google,htmldumpcsv,dump-json>
    #vstatusType    <NAME> abs			# <abs|rel> show absolute or relative values
    #vstatusComment <NAME> Test			# comment

    vstatusFilter  csv 0 1 2 3 4 404 5	# summarize error-codes (0 = all errors, 1 = 1xx ...)
    vstatusFormat  csv csv
    vstatusType    csv abs
    vstatusComment csv Test
    
    vstatusFilter  csv-rel 0 1 2 3 4 5
    vstatusFormat  csv-rel csv
    vstatusType    csv-rel rel
    vstatusDelta   csv-rel 60
    vstatusComment csv-rel "Shows all, 1xx,2xx,3xx,4xx,5xx codes based on the last 60 seconds"
    
    vstatusFilter  google 0 1 2 3 4 5
    vstatusFormat  google google
    vstatusDelta   google 60
    vstatusComment google "Data for google chart tools"


### Parameters

First, there are two parameter, vstatusHistSize and vstatusGranularity. These parameters controlls, wow many data values are stored, and how often, they are renewed.

vstatusHistSize controls, how many lines the data-table woll hold for each vhost.
e.g. vstatusHistSize 120 will hold 120 lines for each vhost.

vstatusGranularity will control the amount of time in seconds, a line will be active.

e.g. vstatusGranularity 1 will create a new line every second.

Therefor, vstatusHistSize 120 and vstatusGranularity 1 will hold data for two minutes


### Filters

For getting the data stored in the internal table, you will need a filter. Filters requires several parameters.

A Filter will be create on the fly, by naming it as the fist parameter i one of the following commands: vstatusFilter, vstatusFormat, vstatusType, vstatusDelta, vstatusDelta

#### vstatusFilter
vstatusFilter <name> <filter> declares, which response-codes will be exported.
0 - summarize all status-codes (all hits)
1 - summarize all 1xx-codes
...
5 - summarize all 5xx-codes

100 - list all 100-codes
404 - list all "Page not found" codes

#### vstatusFormat
vstatusFormat <name> <format> declares, how the data will be formatted
valid statements are:
    html      - creates human readable output
    csv       - creates csv-content <timestamp>,<vhost>,<codes as declared in vstatusFilter>
    json      - creates a json-object containing the colelcted data.
    google    - creates a google-json for using in google chart-tools
    dump-json - dump the complete internal table as a json-object
    dump-csv  - dump the complete internal table using csv-output
    
#### vstatusType
vstatusType <name> <type> declares, if the data will be delta or absolut values
<abs> - raw hits will be displayed. this counter will be resettet, if apache is restarted
<rel> - delta hits. val = val(newest non-active line) - val(newest non-active line - vstatusDelta)

#### vstatusDelta
vstatusDelta <name> <delta> controls the timeperiod used for <vstatusType = rel>. If missing, 1 will be set

#### vstatusComment
vstatusComment <name> <comment> just a comment for the Filter. Shown in the overview-page

### accessing the data
All data can be accessed via get-requests. The example defines a Location called "vstatus".
getting http://<ip>/vstatus will show you a overview-page with several informations including a list of all defined filters.
Clicking a filter will show you the data, defined by the filter.



###Author - [Christopher Kreitz](https://github.com/romses)
