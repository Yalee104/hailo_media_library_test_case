# H15 Hailo Media Library C++ Test Case reference code

The purpose of this inference application example is to provide a reference code of few test cases to test every release on hailo media library

## Content
* Each test is under specific test folder
* Each test contains its own frintend and encode json files
* The file pidstatloop.sh samples the pidstat every 5 minutes and saved it to a file, the purpose is to check if the running application has memory leak overtime by checking the RSS value. This is usually used to test stream example first by sampling at least 6+ hours, if memory does not leak overtime then the test extend to denoise and hdr to see if when switching on/off would have memory leak.

## WARNING
* This may not be compatible with future media library version as frontend and encoder json file will change and therefore all test example's json file will have to be revised for the specific hailo media library release.

