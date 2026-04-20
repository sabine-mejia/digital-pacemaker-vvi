#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <string>
#include <tuple>
#include <vector>
#include <mqtt/async_client.h>

using namespace std;
using namespace std::chrono;

const string ADDRESS { "tcp://mqtt-dev.precise.seas.upenn.edu" };
const string USERNAME { "cis441-541_2025" };
const string PASSWORD { "cukwy2-geNwit-puqced" };

const int QOS = 1;

const int W = 20; // Time Window
const int P = 5; // Interval of how often we check

const int LRI = 1500; // Slowest allowed interval when pacing: 40 bpm
const int URI = 333; // Fastest interval when pacing: 180 bpm
const int VRP = 150; // Ventricular Refractory Period
const int HRI = 1600; // Hysteris interval
const int URL = 180; // Fastest bpm the heart can pace

float pace_ratio_threshold = 0.6; // If pacing exceeds this threshold, need to raise an alarm as heart is not producing enough natural beats

unsigned long last_event = 0;
bool hp_enabled = false;
std::vector<std::tuple<unsigned long, std::string>> events;

// communication between Virtual Component and Virtual Patient
const string UPDATE_TOPIC {"cis441-541/PacingAhead/Update"}; // Message received from heart

// Separate callback class inheriting from mqtt::callback
class MessageRelayCallback : public virtual mqtt::callback {
    mqtt::async_client& client_;

public:
    MessageRelayCallback(mqtt::async_client& client)
        : client_(client) {}

    // subscribe mqtt topics
    void connected(const string& cause) override {
        cout <<"Trying" << endl;
        client_.subscribe(UPDATE_TOPIC, QOS); // Subscribe to Topic 4 from Virtual Patient
        cout << "Subscribed!" << endl;
    }

    void check_assurance_cases(unsigned long interval, const string& type){
        int tv = interval;
        const int timing_tolerance = 8; // Specification of +- 8m/s

        cout << "[MONITOR] Started check for assurance cases...." << endl;

        // PropLRI case
        if (!hp_enabled && tv > (LRI + timing_tolerance)){
            cout <<"[ERROR] PropLRI case has been violated!" << endl;
        }
        // PropHRI case
        if (hp_enabled && tv > (HRI + timing_tolerance)){
            cout <<"[ERROR] PropHRI case has been violated!" << endl;
        }
        // PropVRP case
        if (tv <= (VRP - timing_tolerance)){
            cout << "[ERROR] PropVRP has been violated!" << endl;
        }
        // Deadlock case - add cushion of timing tolerance to HRI because it should never go beyond HRI
        if (tv > (HRI + timing_tolerance)){
            cout << "[ERROR] Deadlock has been violated!" << endl;
        }
        cout << "[MONITOR] Completed check for assurance cases" << endl;
    }

    // message handler
    void message_arrived(mqtt::const_message_ptr msg) override {
        string topic = msg->get_topic();
        string payload_raw = msg->to_string();

        unsigned long now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        size_t split_on = payload_raw.find(":");
        if (split_on == string::npos){
            return;
        }
        string type = payload_raw.substr(0, split_on);
        int interval = stoi(payload_raw.substr(split_on + 1));

        cout << "[MONITOR] Received a signal from controller about ventricular event: " << type << " with interval " << interval << endl;

        check_assurance_cases(interval, type);

        if (type == "VSense"){
            cout << "[MONITOR] Naturally paced via heart" << endl;
            if (!hp_enabled){
                cout << "[MONITOR] Hystersis activated" << endl;
            }
            hp_enabled = true;
        }
        if (type == "VPace"){
            cout << "[MONITOR] Artifically Paced via controller" << endl;

            if (hp_enabled){
                cout << "[MONITOR] Hystersis deactivated" << endl;
            }
            hp_enabled = false;
        }

        events.emplace_back(now_ms, type);
        last_event = now_ms;
    }
};

// The main MQTTClientHandler class to manage the connection and the callback
class MQTTClientHandler {
    mqtt::async_client client_;
    MessageRelayCallback callback_;

public:
    MQTTClientHandler(const string& host, const string& username, const string& password)
        : client_(host, ""), callback_(client_) {

        // set callback functions
        client_.set_callback(callback_);

        // connect to mqtt
        auto connOpts = mqtt::connect_options_builder().clean_session().automatic_reconnect().user_name(username).password(password).finalize();
        try{
            cout << "Connecting..." << endl;
            auto connTok = client_.connect(connOpts);
            cout << "Waiting for connect..." << endl;
            connTok->wait();
            cout << "Connected to broker succesfully!" << endl;
        }
        catch(const mqtt::exception& ex) {
            cerr << "\nERROR: unable to connect, " << ex << endl;
        }
    }

    // This keeps the program running indefinitely, which ensures that the MQTT client remains active and ready to receive and send messages.
    void inject_loop() {
        cout << "Press Ctrl+C to exit the mqtt handler." << endl;
        while (true) {
            this_thread::sleep_for(milliseconds(200));
        }
    }
};

void window_analysis(){
    while (true){
        this_thread::sleep_for(seconds(P));

        // Calculate the start of the window from the current point in time
        unsigned long now_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        unsigned long window_begin = now_ms - (W * 1000);

        int senses = 0;
        int paces = 0;

        double total_bpm = 0;
        int beats = 0;
        bool entered = false;
        unsigned long lastBeat = 0;

        for (auto &[timestamp, type]: events){
            if (timestamp >= window_begin){

                if (type == "VSense"){
                    senses += 1;
                }
                if (type == "VPace"){
                    paces += 1;
                }

                if (entered){
                    auto interval = timestamp - lastBeat;
                    if (interval > 0){
                        total_bpm += 60000/(interval); // BPM Formula
                        beats += 1;
                    }
                }

                entered = true;
                lastBeat = timestamp;
            }
        }

        cout << "----------------------" << endl;
        cout << "Window Analysis of beats in last " << P << " seconds" << endl;

        if (entered){
            cout << "Pace count is " << paces << endl;
            cout << "Senses count is " << senses << endl;

            double avg_bpm = (total_bpm)/(double)beats;
            cout << "Average BPM is " << avg_bpm << endl;

            double pace_ratio = ((double)paces)/(senses + paces);
            cout << "Pacing ratio is " << pace_ratio << endl;

            if (pace_ratio > pace_ratio_threshold){
                cout << "[ALERT] Too much pacing: Patient in danger" << endl;
            }
            if (avg_bpm > URL){
                cout << "[ALERT] Heart beating too fast: Patient in danger" << endl;
            }
        }
        cout << "-----------------------------" << endl;
    }
}

int main() {
    MQTTClientHandler mqtt_handler(ADDRESS, USERNAME, PASSWORD);

    std::thread analysis_thread(window_analysis);
    mqtt_handler.inject_loop();
    analysis_thread.join();

    return 0;
}