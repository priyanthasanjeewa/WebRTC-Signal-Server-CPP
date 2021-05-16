
port=18099
log_file=log/signalling_server.log

if [ -f "$log_file" ]
then
  timestamp=$(date +'%Y%m%d.%H%M%S.%N')
  mv $log_file "${log_file}.${timestamp}"
fi

./signalling_server $port > $log_file 2>&1 &

ps ux | grep signalling_server | grep -v grep
