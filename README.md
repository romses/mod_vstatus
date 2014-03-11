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


###Author - [Christopher Kreitz](https://github.com/romses)
