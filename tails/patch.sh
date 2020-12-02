#!/bin/bash -e

tails_location="$1"

if [ -z $tails_location ]
then
    echo "Specify tails config directory please"
    exit 1
fi

if ! [ -f $tails_location/amnesia ]
then
    echo "Tails config directory is invalid"
    exit 1
fi

for file in $(find . -type "f,l" -not -name "patch.sh")
do
    echo "$file -> $tails_location/$file"
    mkdir -p $tails_location/$(dirname $file)
    cp -r $file $tails_location/$file
done

echo "Done"
