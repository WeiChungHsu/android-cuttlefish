#!/bin/bash

# Step 1. check if we need to restrict cpus per user
total_cpu=$(nproc --all)
counter=0
uid=1000
cpus_per_instance=12
upper_bound=$[$cpus_per_instance -1]
while [ "$total_cpu" -gt "$upper_bound" ]
do
  #systemctl set-property user-1000.slice AllowedCPUs=0-11
  systemctl set-property user-$uid.slice AllowedCPUs=$counter-$upper_bound
  counter=$[$counter +$cpus_per_instance]
  upper_bound=$[$upper_bound +$cpus_per_instance]
  uid=$[$uid +1]
done

# Step 2. Build Cuttlefish docker image if there is no such one
cf_image=$(docker image list | grep "cuttlefish-orchestration")
if [ "$cf_image" == "" ]; then
  # Build CF docker image
  cd /home/vsoc-01
  if [ ! -d "/home/vsoc-01/android-cuttlefish-stable" ]; then
    echo "wget https://github.com/google/android-cuttlefish/archive/refs/tags/stable.zip" > android-cuttlefish.log
    wget https://github.com/google/android-cuttlefish/archive/refs/tags/stable.zip
    unzip stable.zip
  fi
  cd android-cuttlefish-stable/docker
  /bin/bash image-builder.sh &> build.log
fi

# Step 3. Run CO server
# use the fixed version instead of HEAD, makes it easier to triage problems when they arise
CO_VERSION="0.1.0-alpha"
cd /home/vsoc-01
if [ ! -d "/home/vsoc-01/cloud-android-orchestration-$CO_VERSION" ]; then
  echo "wget https://github.com/google/cloud-android-orchestration/archive/refs/tags/v$CO_VERSION.zip" > cloud-android-orchestration.log
  wget https://github.com/google/cloud-android-orchestration/archive/refs/tags/v$CO_VERSION.zip
  unzip v$CO_VERSION.zip
fi
cd cloud-android-orchestration-$CO_VERSION # Root directory of this repository
/bin/bash scripts/docker/run.sh &> run.log
