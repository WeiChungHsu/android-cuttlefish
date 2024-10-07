#
# Regular cron jobs for the metapackage-google package.
#
# Workarounds for Network-Manager
@reboot root sleep 100 && /usr/sbin/dhclient
@reboot root sleep 150 && su vsoc-01 -c '/bin/bash /usr/share/metapackage-google/cf_docker_co_run.sh'
@reboot root sleep 200 && systemctl set-property user-1000.slice AllowedCPUs=0-11 && systemctl set-property user-1001.slice AllowedCPUs=12-23 && systemctl set-property user-1002.slice AllowedCPUs=24-35 && systemctl set-property user-1003.slice AllowedCPUs=36-47 && systemctl set-property user-1004.slice AllowedCPUs=48-59 && systemctl set-property user-1005.slice AllowedCPUs=60-71 && systemctl set-property user-1006.slice AllowedCPUs=72-83 && systemctl set-property user-1007.slice AllowedCPUs=84-95 && systemctl set-property user-1008.slice AllowedCPUs=96-107 && systemctl set-property user-1009.slice AllowedCPUs=108-119
