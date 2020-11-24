
count=1
while [$count -le 10]
do
    echo $count
    ((count++))
done
ZZ
if [ “$1” = “-v” ] # using = for equality is POSIX
then
    VERBOSE=1
elif [ “$1” = “-c” ]
then
    COMPRESS=1
else
    echo “confused!”
fi

for i in {1...5}
do
    echo $i
done

for file in *.c
do
    gcc -o ${file%.c} $file
done
