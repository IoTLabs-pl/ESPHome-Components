# Panasonic Aquarea Entities - By Type

## `sensor`

| Name | Heishamon ID | Unit | Device Class | Icon | State Class | Example |
|------|--------------|------|--------------|------|-------------|---------|
| Pump Flow | `TOP1` | L/min | volume_flow_rate | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 1</code></pre> |
| Main Inlet Temperature | `TOP5` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 5</code></pre> |
| Main Outlet Temperature | `TOP6` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 6</code></pre> |
| Main Target Temperature | `TOP7` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 7</code></pre> |
| Compressor Frequency | `TOP8` | Hz | frequency | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 8</code></pre> |
| DHW Target Temperature | `TOP9` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 9</code></pre> |
| DHW Temperature | `TOP10` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 10</code></pre> |
| Operations Hours | `TOP11` | h | duration | — | total_increasing | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 11</code></pre> |
| Operations Counter | `TOP12` | — | — | mdi:counter | total_increasing | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 12</code></pre> |
| Outside Temperature | `TOP14` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 14</code></pre> |
| Heat Power Production | `TOP15` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 15</code></pre> |
| Heat Power Consumption | `TOP16` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 16</code></pre> |
| Outside Pipe Temperature | `TOP21` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 21</code></pre> |
| Heat Delta | `TOP23` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 23</code></pre> |
| Cool Delta | `TOP24` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 24</code></pre> |
| DHW Holiday Shift Temperature | `TOP25` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 25</code></pre> |
| Room Thermostat Temperature | `TOP33` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 33</code></pre> |
| Water Temperature Zone 1 | `TOP36` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 36</code></pre> |
| Water Temperature Zone 2 | `TOP37` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 37</code></pre> |
| Cool Power Production | `TOP38` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 38</code></pre> |
| Cool Power Consumption | `TOP39` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 39</code></pre> |
| DHW Power Production | `TOP40` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 40</code></pre> |
| DHW Power Consumption | `TOP41` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 41</code></pre> |
| Water Target Temperature Zone 1 | `TOP42` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 42</code></pre> |
| Water Target Temperature Zone 2 | `TOP43` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 43</code></pre> |
| Room Holiday Shift Temperature | `TOP45` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 45</code></pre> |
| Buffer Temperature | `TOP46` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 46</code></pre> |
| Solar Temperature | `TOP47` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 47</code></pre> |
| Pool Temperature | `TOP48` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 48</code></pre> |
| Main Heat Exchanger Outlet Temperature | `TOP49` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 49</code></pre> |
| Discharge Temperature | `TOP50` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 50</code></pre> |
| Inside Pipe Temperature | `TOP51` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 51</code></pre> |
| Defrost Temperature | `TOP52` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 52</code></pre> |
| Eva Outlet Temperature | `TOP53` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 53</code></pre> |
| Bypass Outlet Temperature | `TOP54` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 54</code></pre> |
| IPM Temperature | `TOP55` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 55</code></pre> |
| Temperature Zone 1 | `TOP56` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 56</code></pre> |
| Temperature Zone 2 | `TOP57` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 57</code></pre> |
| Fan1 Motor Speed | `TOP62` | RPM | — | mdi:fan | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 62</code></pre> |
| Fan2 Motor Speed | `TOP63` | RPM | — | mdi:fan | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 63</code></pre> |
| High Pressure | `TOP64` | kgf/cm² | pressure | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 64</code></pre> |
| Pump Speed | `TOP65` | RPM | — | mdi:pump | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 65</code></pre> |
| Low Pressure | `TOP66` | kgf/cm² | pressure | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 66</code></pre> |
| Compressor Current | `TOP67` | A | current | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 67</code></pre> |
| Sterilization Temperature | `TOP70` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 70</code></pre> |
| Sterilization Max Time | `TOP71` | min | duration | — | — | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 71</code></pre> |
| Heating Off Outdoor Temperature | `TOP77` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 77</code></pre> |
| Heater On Outdoor Temperature | `TOP78` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 78</code></pre> |
| Heat to Cool Temperature | `TOP79` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 79</code></pre> |
| Cool to Heat Temperature | `TOP80` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 80</code></pre> |
| Room Heater Operations Hours | `TOP90` | h | duration | — | total_increasing | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 90</code></pre> |
| DHW Heater Operations Hours | `TOP91` | h | duration | — | total_increasing | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 91</code></pre> |
| Pump Duty | `TOP93` | — | — | mdi:pump | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 93</code></pre> |
| Solar On Delta | `TOP102` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 102</code></pre> |
| Solar Off Delta | `TOP103` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 103</code></pre> |
| Solar Frost Protection | `TOP104` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 104</code></pre> |
| Solar High Limit | `TOP105` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 105</code></pre> |
| Buffer Tank Delta | `TOP113` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 113</code></pre> |
| Water Pressure | `TOP115` | bar | pressure | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 115</code></pre> |
| Second Inlet Temperature | `TOP116` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 116</code></pre> |
| Economizer Outlet Temperature | `TOP117` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 117</code></pre> |
| Second Room Thermostat Temperature | `TOP118` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 118</code></pre> |
| Valve PID Zone 1 | `TOP127` | % | — | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 127</code></pre> |
| Valve PID Zone 2 | `TOP128` | % | — | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 128</code></pre> |
| Bivalent Start Temperature | `TOP131` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 131</code></pre> |
| Bivalent Advanced Start Temperature | `TOP134` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 134</code></pre> |
| Bivalent Advanced Stop Temperature | `TOP135` | °C | temperature | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 135</code></pre> |
| Bivalent Advanced Start Delay | `TOP136` | min | duration | — | — | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 136</code></pre> |
| Bivalent Advanced Stop Delay | `TOP137` | min | duration | — | — | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 137</code></pre> |
| Bivalent Advanced DHW Delay | `TOP138` | min | duration | — | — | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 138</code></pre> |
| Heat Power Consumption Extra | `TOPx0` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x0</code></pre> |
| Cool Power Consumption Extra | `TOPx1` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x1</code></pre> |
| DHW Power Consumption Extra | `TOPx2` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x2</code></pre> |
| Heat Power Production Extra | `TOPx3` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x3</code></pre> |
| Cool Power Production Extra | `TOPx4` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x4</code></pre> |
| DHW Power Production Extra | `TOPx5` | W | power | — | measurement | <pre lang="yaml"><code>sensor:&#13;  - platform: panasonic_aquarea&#13;    top: x5</code></pre> |

## `number`

| Name | Heishamon ID | Device Class | Min | Max | Entity Category | Example |
|------|--------------|--------------|-----|-----|-----------------|---------|
| Heat Request Zone 1 | `SET5` | temperature | -5 | 5 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 5</code></pre> |
| Cool Request Zone 1 | `SET6` | temperature | -5 | 5 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 6</code></pre> |
| Heat Request Zone 2 | `SET7` | temperature | -5 | 5 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 7</code></pre> |
| Cool Request Zone 2 | `SET8` | temperature | -5 | 5 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 8</code></pre> |
| DHW Temperature | `SET11` | temperature | 40 | 75 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 11</code></pre> |
| Max Pump Duty | `SET15` | — | 64 | 254 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 15</code></pre> |
| Heat Target High Zone 1 | `SET16:1` | temperature | 20 | 60 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:1</code></pre> |
| Heat Target Low Zone 1 | `SET16:2` | temperature | 20 | 60 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:2</code></pre> |
| Heat Outside Low Zone 1 | `SET16:3` | temperature | -20 | 15 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:3</code></pre> |
| Heat Outside High Zone 1 | `SET16:4` | temperature | -20 | 15 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:4</code></pre> |
| Heat Target High Zone 2 | `SET16:5` | temperature | 20 | 60 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:5</code></pre> |
| Heat Target Low Zone 2 | `SET16:6` | temperature | 20 | 60 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:6</code></pre> |
| Heat Outside Low Zone 2 | `SET16:7` | temperature | -20 | 15 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:7</code></pre> |
| Heat Outside High Zone 2 | `SET16:8` | temperature | -20 | 15 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:8</code></pre> |
| Cool Target High Zone 1 | `SET16:9` | temperature | 5 | 20 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:9</code></pre> |
| Cool Target Low Zone 1 | `SET16:10` | temperature | 5 | 20 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:10</code></pre> |
| Cool Outside Low Zone 1 | `SET16:11` | temperature | 15 | 30 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:11</code></pre> |
| Cool Outside High Zone 1 | `SET16:12` | temperature | 15 | 30 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:12</code></pre> |
| Cool Target High Zone 2 | `SET16:13` | temperature | 5 | 20 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:13</code></pre> |
| Cool Target Low Zone 2 | `SET16:14` | temperature | 5 | 20 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:14</code></pre> |
| Cool Outside Low Zone 2 | `SET16:15` | temperature | 15 | 30 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:15</code></pre> |
| Cool Outside High Zone 2 | `SET16:16` | temperature | 15 | 30 | config | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 16:16</code></pre> |
| Floor Heat Delta | `SET18` | temperature | 1 | 15 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 18</code></pre> |
| Floor Cool Delta | `SET19` | temperature | 1 | 15 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 19</code></pre> |
| DHW Heat Delta | `SET20` | temperature | -12 | -2 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 20</code></pre> |
| Heater Delay Time | `SET21` | duration | 0 | 250 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 21</code></pre> |
| Heater Start Delta | `SET22` | temperature | 1 | 15 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 22</code></pre> |
| Heater Stop Delta | `SET23` | temperature | 1 | 15 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 23</code></pre> |
| Buffer Delta | `SET27` | temperature | 0 | 10 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 27</code></pre> |
| Heating Off Outdoor Temp | `SET29` | temperature | 5 | 35 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 29</code></pre> |
| Bivalent Start Temp | `SET36` | temperature | -15 | 35 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 36</code></pre> |
| Bivalent AP Start Temp | `SET37` | temperature | -10 | 0 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 37</code></pre> |
| Bivalent AP Stop Temp | `SET38` | temperature | -10 | 0 | — | <pre lang="yaml"><code>number:&#13;  - platform: panasonic_aquarea&#13;    set: 38</code></pre> |

## `select`

| Name | Heishamon ID | Icon | Options | Example |
|------|--------------|------|---------|---------|
| Holiday Mode | `SET2` | mdi:palm-tree | <ul><li>Active</li><li>Off</li><li>Scheduled</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 2</code></pre> |
| Quiet Mode Level | `SET3` | mdi:sleep | <ul><li>Level 1</li><li>Level 2</li><li>Level 3</li><li>Off</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 3</code></pre> |
| Powerful Mode Time | `SET4` | mdi:arm-flex | <ul><li>30min</li><li>60min</li><li>90min</li><li>Off</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 4</code></pre> |
| Operation Mode | `SET9` | mdi:thermostat | <ul><li>Auto</li><li>Auto (Cool)</li><li>Auto (Cool) + Tank</li><li>Auto (Heat)</li><li>Auto (Heat) + Tank</li><li>Auto + Tank</li><li>Cool</li><li>Cool + Tank</li><li>Heat</li><li>Heat + Tank</li><li>Tank</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 9</code></pre> |
| Zones | `SET17` | mdi:texture-box | <ul><li>Zone 1</li><li>Zone 1 & 2</li><li>Zone 2</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 17</code></pre> |
| External Pad Heater | `SET26` | mdi:radiator | <ul><li>Disabled</li><li>Type-A</li><li>Type-B</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 26</code></pre> |
| Bivalent Mode | `SET35` | — | <ul><li>Advanced Parallel</li><li>Alternative</li><li>Parallel</li></ul> | <pre lang="yaml"><code>select:&#13;  - platform: panasonic_aquarea&#13;    set: 35</code></pre> |

## `binary_sensor`

| Name | Heishamon ID | Device Class | Example |
|------|--------------|--------------|---------|
| Quiet Mode Schedule | `TOP3` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 3</code></pre> |
| Defrosting | `TOP26` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 26</code></pre> |
| Internal Heater | `TOP60` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 60</code></pre> |
| External Heater | `TOP61` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 61</code></pre> |
| Force Heater | `TOP68` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 68</code></pre> |
| Sterilization | `TOP69` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 69</code></pre> |
| DHW Installed | `TOP100` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 100</code></pre> |
| Anti Freeze Mode | `TOP109` | running | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 109</code></pre> |
| Optional PCB | `TOP110` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 110</code></pre> |
| Pump Zone 2 | `TOP123` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 123</code></pre> |
| Pump Zone 1 | `TOP124` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 124</code></pre> |
| Bivalent Advanced Heat | `TOP132` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 132</code></pre> |
| Bivalent Advanced DHW | `TOP133` | — | <pre lang="yaml"><code>binary_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 133</code></pre> |

## `text_sensor`

| Name | Heishamon ID | Icon | Options | Example |
|------|--------------|------|---------|---------|
| Three-way Valve State | `TOP20` | mdi:pipe-valve | <ul><li>Buffer Tank</li><li>DHW Tank</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 20</code></pre> |
| Error | `TOP44` | — | — | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 44</code></pre> |
| DHW Heater State | `TOP58` | mdi:cancel | <ul><li>Blocked</li><li>Free</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 58</code></pre> |
| Room Heater State | `TOP59` | mdi:cancel | <ul><li>Blocked</li><li>Free</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 59</code></pre> |
| Heating Mode | `TOP76` | mdi:fire | <ul><li>Comp. Curve</li><li>Direct</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 76</code></pre> |
| Cooling Mode | `TOP81` | mdi:snowflake | <ul><li>Comp. Curve</li><li>Direct</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 81</code></pre> |
| Heat Pump Model | `TOP92` | mdi:heat-pump | <ul><li>IDU:WH-ADC0309H3E5, ODU:WH-UD09HE5</li><li>IDU:WH-ADC0309J3E5, ODU:WH-UD05JE5</li><li>IDU:WH-ADC0309J3E5, ODU:WH-UD09JE5</li><li>IDU:WH-ADC0309J3E5C, ODU:WH-UD03JE5</li><li>IDU:WH-ADC0309J3E5C, ODU:WH-UD07JE5</li><li>IDU:WH-ADC0309K3E5, ODU:WH-UDZ09KE5</li><li>IDU:WH-ADC0309K3E5AN, ODU:WH-UDZ07KE5</li><li>IDU:WH-ADC0509L3E5, ODU:WH-WDG07LE5</li><li>IDU:WH-ADC0509L3E5AN, ODU:WH-WDG05LE5</li><li>IDU:WH-ADC0912H9E8, ODU:WH-UX12HE8</li><li>IDU:WH-ADC0916H9E8, ODU:WH-UX09HE8</li><li>IDU:WH-ADC0916H9E8, ODU:WH-UX12HE8</li><li>IDU:WH-ADC0916H9E8, ODU:WH-UX12HE8</li><li>IDU:WH-ADC0916H9E8, ODU:WH-UX16HE8</li><li>IDU:WH-ADC1216H6E5, ODU:WH-UD12HE5</li><li>IDU:WH-ADC1216H6E5C, ODU:WH-UD12HE5</li><li>IDU:WH-SDC0305J3E5, ODU:WH-UD05JE5</li><li>IDU:WH-SDC0309K3E5, ODU:WH-UDZ05KE5</li><li>IDU:WH-SDC0309K3E5, ODU:WH-UDZ07KE5</li><li>IDU:WH-SDC0509L3E5, ODU:WH-WDG05LE5</li><li>IDU:WH-SDC0509L3E5, ODU:WH-WDG07LE5</li><li>IDU:WH-SDC0509L3E5, ODU:WH-WDG09LE5</li><li>IDU:WH-SDC05H3E5, ODU:WH-UD05HE5</li><li>IDU:WH-SDC0709J3E5, ODU:WH-UD07JE5</li><li>IDU:WH-SDC0709J3E5, ODU:WH-UD09JE5</li><li>IDU:WH-SDC07H3E5-1, ODU:WH-UD07HE5-1</li><li>IDU:WH-SDC09H3E5, ODU:WH-UD09HE5</li><li>IDU:WH-SDC09H3E8, ODU:WH-UD09HE8</li><li>IDU:WH-SDC12H9E8, ODU:WH-UD12HE8</li><li>IDU:WH-SQC09H3E8, ODU:WH-UQ09HE8</li><li>IDU:WH-SQC09H3E8, ODU:WH-UQ09HE8</li><li>IDU:WH-SQC12H9E8, ODU:WH-UQ12HE8</li><li>IDU:WH-SXC09H3E5, ODU:WH-UX09HE5</li><li>IDU:WH-SXC09H3E5, ODU:WH-UX09HE5</li><li>IDU:WH-SXC09H3E8, ODU:WH-UX09HE8</li><li>IDU:WH-SXC09H3E8, ODU:WH-UX09HE8</li><li>IDU:WH-SXC12H6E5, ODU:WH-UX12HE5</li><li>IDU:WH-SXC12H9E8, ODU:WH-UX12HE8</li><li>IDU:WH-SXC16H9E8, ODU:WH-UX16HE8</li><li>WH-MDC05H3E5</li><li>WH-MDC05J3E5</li><li>WH-MDC05J3E5</li><li>WH-MDC07H3E5</li><li>WH-MDC07J3E5</li><li>WH-MDC07J3E5</li><li>WH-MDC09H3E5</li><li>WH-MDC09J3E5</li><li>WH-MDC09J3E5</li><li>WH-MXC09H3E5</li><li>WH-MXC09J3E5</li><li>WH-MXC09J3E8</li><li>WH-MXC12H9E8</li><li>WH-MXC12J6E5</li><li>WH-MXC12J9E8</li><li>WH-MXC16J9E8</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 92</code></pre> |
| Solar Mode | `TOP101` | mdi:solar-power-variant | <ul><li>Buffer Tank</li><li>DHW Tank</li><li>Disabled</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 101</code></pre> |
| Pump Flowrate Mode | `TOP106` | mdi:pump | <ul><li>DeltaT</li><li>Max flow</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 106</code></pre> |
| Liquid Type | `TOP107` | mdi:water | <ul><li>Glycol</li><li>Water</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 107</code></pre> |
| Sensor Settings Zone 1 | `TOP111` | — | <ul><li>External Thermostat</li><li>Internal Thermostat</li><li>Thermistor</li><li>Water Temperature</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 111</code></pre> |
| Sensor Settings Zone 2 | `TOP112` | — | <ul><li>External Thermostat</li><li>Internal Thermostat</li><li>Thermistor</li><li>Water Temperature</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 112</code></pre> |
| Two-way Valve State | `TOP125` | mdi:pipe-valve | <ul><li>Cool</li><li>Heat</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 125</code></pre> |
| Three-way Valve State 2 | `TOP126` | mdi:pipe-valve | <ul><li>Buffer Tank</li><li>DHW Tank</li></ul> | <pre lang="yaml"><code>text_sensor:&#13;  - platform: panasonic_aquarea&#13;    top: 126</code></pre> |

## `button`

| Name | Heishamon ID | Example |
|------|--------------|---------|
| Force Defrosting | `SET12` | <pre lang="yaml"><code>button:&#13;  - platform: panasonic_aquarea&#13;    set: 12</code></pre> |
| Force Sterilization | `SET13` | <pre lang="yaml"><code>button:&#13;  - platform: panasonic_aquarea&#13;    set: 13</code></pre> |
| Set Pump Service Mode | `SET141` | <pre lang="yaml"><code>button:&#13;  - platform: panasonic_aquarea&#13;    set: 141</code></pre> |
| Set Pump Max Speed | `SET142` | <pre lang="yaml"><code>button:&#13;  - platform: panasonic_aquarea&#13;    set: 142</code></pre> |

## `switch`

| Name | Heishamon ID | Example |
|------|--------------|---------|
| State | `SET1` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 1</code></pre> |
| Force DHW | `SET10` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 10</code></pre> |
| Main Schedule | `SET24` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 24</code></pre> |
| Alt External Sensor | `SET25` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 25</code></pre> |
| Buffer Installed | `SET28` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 28</code></pre> |
| External Control | `SET30` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 30</code></pre> |
| External Error Signal | `SET31` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 31</code></pre> |
| External Compressor Control | `SET32` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 32</code></pre> |
| External Heat Cool Control | `SET33` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 33</code></pre> |
| Bivalent Control | `SET34` | <pre lang="yaml"><code>switch:&#13;  - platform: panasonic_aquarea&#13;    set: 34</code></pre> |