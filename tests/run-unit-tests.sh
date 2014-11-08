#!/bin/bash

function get_date()
{
    # \see http://askubuntu.com/questions/355188/date-format-in-unix

    date +%Y-%m-%dT%H:%M:%S%z
}

# convert text version of unit test results to JUnit XML
function main()
{
    make -s test_private | tee tests.log

    count=$(cat tests.log | wc -l)
    timestamp=$(get_date)

    #echo "line $count"

    (
    echo "<testsuites>"

    for i in $(seq 1 $count)
    do
        line=$(head -n $i tests.log|tail -n1)
        tests=$(echo $line | awk '{print $8}')
        failures=$(echo $line | awk '{print $6}')
        name=$(echo $line | awk '{print $2}')

        #echo $timestamp

        # \see http://nelsonwells.net/2012/09/how-jenkins-ci-parses-and-displays-junit-output/
        echo "    <testsuite name=\"$name\" tests=\"$tests\" failures=\"$failures\" timestamp=\"$timestamp\">"
        echo "    </testsuite>"
    done

    echo "</testsuites>"
    ) > unit-tests.junit.xml

    echo "see unit-tests.junit.xml"

    tests/summarize-tests.sh tests.log
}

main
