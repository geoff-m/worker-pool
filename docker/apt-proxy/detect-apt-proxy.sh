#!/bin/bash

proxy_name=package-proxy.geoff.space
proxy_port=3142

nc -w1 -z $proxy_name $proxy_port > /dev/null 2>&1
reachable=$?
if [ "$reachable" -eq 0 ]; then
  # Tell apt to use this proxy
  echo http://$proxy_name:$proxy_port
else
  # Tell apt not to use proxy
  echo DIRECT
fi