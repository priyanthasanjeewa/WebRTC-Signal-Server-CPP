
ps ux | grep signalling_server | grep -v grep

pid=`ps ux | grep signalling_server | grep -v grep | awk '{print $2}'`
if [ -z "$pid" ]
then
  echo "singalling_server not running"
  exit
fi

kill "$pid"
echo "singalling_server stopped"
