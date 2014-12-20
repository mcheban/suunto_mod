var locationOptions = { 
  timeout: 15000, 
  maximumAge: 60*1000 
};

var FORECAST_IO = 'forecastio';
var FORECAST_IO_URL = 'https://api.forecast.io/forecast/0ed85ffbcfd287fc0b2f32d722391378/{0},{1}' + 
                          '?units=si&exclude=minutely,hourly,daily,alerts,flags';
  
var OPEN_WEATHER = 'openweather';
var OPEN_WEATHER_URL = 'http://api.openweathermap.org/data/2.5/weather?lat={0}&lon={1}';

var weatherSystems = {};
weatherSystems[FORECAST_IO] = FORECAST_IO_URL;
weatherSystems[OPEN_WEATHER] = OPEN_WEATHER_URL;

var weatherOptions = {
  system: FORECAST_IO,
  maximumAge: 30*60*1000
};

var OPENEXCHANGERATES = 'openexchangerates';
var OPENEXCHANGERATES_URL = 'https://openexchangerates.org/api/latest.json?app_id=377420c3866942e9a4d1012c4a44edc8&base=USD';
var EXCHANGERATELAB = 'exchangeratelab';
var EXCHANGERATELAB_URL = 'http://api.exchangeratelab.com/api/single/RUB?apikey=E41AA448FA0F57C94684981E3D650CCF';
var YAHOO = 'yahoo';
var YAHOO_URL = 'http://finance.yahoo.com/webservice/v1/symbols/RUB=X/quote?format=json';
  
var rateSystems = {};
rateSystems[OPENEXCHANGERATES] = OPENEXCHANGERATES_URL;
rateSystems[EXCHANGERATELAB] = EXCHANGERATELAB_URL;
rateSystems[YAHOO] = YAHOO_URL;

var rateOptions = {
  system: YAHOO,
  maximumAge: 30*60*1000
};

if (!String.format) {
  String.format = function(format) {
    var args = Array.prototype.slice.call(arguments, 1);
    return format.replace(/{(\d+)}/g, function(match, number) {
      return typeof args[number] != 'undefined' ? args[number] : match;
    });
  };
}

function currentTimeMillis() {
  return new Date().getTime();
}

var Timer = function() {
  this.times = {};
  this.timeStart = function(label) {
    console.log(label + ': timer started');
    this.times[label] = currentTimeMillis();
  };

  this.timeEnd = function(label) {
    var started = this.times[label];
    if(started) {
      console.log(label + ': ' + (currentTimeMillis() - started) + ' msec');
    }
  };
};

var timer = new Timer();

var ajax = function (options) {
  var xhr = new XMLHttpRequest();
  if(options.onload) {
    xhr.onload = function () { options.onload(this.responseText, xhr); };
  }
  if(options.timeout) {
    xhr.timeout = options.timeout;
  }
  if(options.ontimeout) {
    xhr.ontimeout = function() { options.ontimeout(this); };
  }
  if(options.onerror) {
    xhr.onerror = function() { options.onerror(this); };
  }
  xhr.open(options.type, options.url);
  xhr.send();
};

function parseWeatherResponse(type, response) {
  switch(type) {
    case OPEN_WEATHER:
      return {
        temperature : Math.round(response.main.temp - 273.15),
        summary: response.weather[0].main
      };
    case FORECAST_IO:
      return {
        temperature : Math.round(response.currently.temperature),
        summary: response.currently.summary
      };
  }
}

function parseRateResponse(type, response) {
  switch(type) {
    case OPENEXCHANGERATES:
      return response.rates.RUB;
    case EXCHANGERATELAB:
      return response.rate.rate;
    case YAHOO:
      return parseFloat(response.list.resources[0].resource.fields.price);
  }
}

function mergeObjects(obj1, obj2){
    var obj3 = {};
    for (var attrname in obj1) { obj3[attrname] = obj1[attrname]; }
    for (var attrname in obj2) { obj3[attrname] = obj2[attrname]; }
    return obj3;
}

function sendAppMessage(dictionary) {
  console.log('Sending ' + JSON.stringify(dictionary));
  Pebble.sendAppMessage(dictionary,
    function(e) {
      console.log('Message sent to Pebble successfully!');
    },
    function(e) {
      console.log('Error sending message to Pebble! Retrying...');
      Pebble.sendAppMessage(dictionary,
        function(e) {
          console.log('Message sent to Pebble successfully!');
        },
        function(e) {
          console.log('Error sending message to Pebble! Retry failed!!!');
        }
      );
    }
  );
}

function sendWeatherFailedMessage() {
  sendAppMessage({ 'failed_weather': 0 });
}

function sendLocationFailedMessage() {
  sendAppMessage({ 'failed_location': 0 });
}

function sendRatesFailedMessage() {
  sendAppMessage({ 'failed_rate': 0 });
}

/*
function geocode(lat, lng, callback) {
  var city = '';
  var geoUrl = String.format('http://maps.googleapis.com/maps/api/geocode/json?latlng={0},{1}&sensor=false', lat, lng);
  timer.timeStart('geocode');
  ajax({ 
    url: geoUrl, 
    type: 'GET', 
    onload : function(responseText, xhr) {
      timer.timeEnd('geocode');
      if(xhr.status != 200) {
        console.log('Status is not 200: ' + xhr.status);
        sendWeatherFailedMessage();
        return;
      }
      var resp = JSON.parse(responseText);
      var results = resp.results;
      for(var idx in results) {
        for(var i in results[idx].address_components) {
          var addr = results[idx].address_components[i];
          if(addr.types == 'locality,political') {
            if(addr.long_name) {
              city = addr.long_name;
            }
          }
        }
      }
      console.log(String.format('City "{0}"', city));
      callback(city);
    },
    timeout: 3000,
    ontimeout: function() { callback(city); },
    onerror: function() { callback(city); }
  });
}
*/

function loadWeather(pos, weatherCallback, failedCallback) {
  console.log(String.format('Weather [lat={0}, lon={1}', pos.coords.latitude, pos.coords.longitude));
  timer.timeStart('weather');
  ajax({
    url: String.format(weatherSystems[weatherOptions.system], pos.coords.latitude, pos.coords.longitude), 
    type: 'GET',
    onload : function(responseText, xhr) {
      timer.timeEnd('weather');
      if(xhr.status != 200) {
        console.log('Status is not 200: ' + xhr.status);
        if(failedCallback) {
          failedCallback();
        }
        return;
      }
      var weatherInfo = parseWeatherResponse(weatherOptions.system, JSON.parse(responseText));
      console.log(String.format('Temperature "{0}"; Conditions "{1}"',
                                weatherInfo.temperature, weatherInfo.summary));
//      geocode(pos.coords.latitude, pos.coords.longitude, function(city) {
        var weatherCache = {
          timestamp: currentTimeMillis(),//city && city.length > 0 ? currentTimeMillis() : 0,
          weather: {
            "temperature": weatherInfo.temperature,
            "summary": weatherInfo.summary//,
            //"city": city
          }
        };
        localStorage.setItem('weatherCache', encodeURIComponent(JSON.stringify(weatherCache)));
        if(weatherCallback) {
          weatherCallback(weatherCache.weather);
        }
//      });
    },
    timeout: 10000,
    ontimeout: failedCallback,
    onerror: failedCallback
  });
}

function loadRate(rateReceivedCallback, failedCallback) {
  timer.timeStart('rate');
  ajax({
    url: String.format(rateSystems[rateOptions.system]), 
    type: 'GET',
    onload : function(responseText, xhr) {
      timer.timeEnd('rate');
      if(xhr.status != 200) {
        console.log('Status is not 200: ' + xhr.status);
        if(failedCallback) {
          failedCallback();
        }
        return;
      }
      var rate = parseRateResponse(rateOptions.system, JSON.parse(responseText));
      console.log(String.format('Rate "{0}"', rate));
      var rateCache = {
        timestamp: currentTimeMillis(),
        rate: rate
      };
      localStorage.setItem('rateCache', encodeURIComponent(JSON.stringify(rateCache)));
      if(rateReceivedCallback) {
        rateReceivedCallback({ rate: "" + Math.round(rateCache.rate * 100) / 100 });
      }
    },
    timeout: 10000,
    ontimeout: failedCallback,
    onerror: failedCallback
  });
}

function detectLocation(locationSuccess, locationError) {
  timer.timeStart('location');
  var locationCacheStr = localStorage.getItem('locationCache');
  if(locationCacheStr) {
    var locationCache = JSON.parse(decodeURIComponent(locationCacheStr));
    if(currentTimeMillis() - locationCache.timestamp < locationOptions.maximumAge) {
      console.log('Use location from cache');
      locationSuccess(locationCache.position);
      return;
    }
  }
  navigator.geolocation.getCurrentPosition(function(pos) {
    timer.timeEnd('location');
    var locationCache = { position: pos, timestamp: currentTimeMillis() };
    localStorage.setItem('locationCache', encodeURIComponent(JSON.stringify(locationCache)));
    locationSuccess(pos);
  }, function() {
    timer.timeEnd('location');
    console.log('Error requesting location!');
    locationError();
  }, locationOptions);
}

function updateInfoToPebble(ignoreCache) {
  var needLoadWeather = true;
  var needLoadRate = true;
  
  if(!ignoreCache) {
    var weatherCacheStr = localStorage.getItem('weatherCache');
    if(weatherCacheStr) {
      var weatherCache = JSON.parse(decodeURIComponent(weatherCacheStr));
      if(currentTimeMillis() - weatherCache.timestamp < weatherOptions.maximumAge) {
        console.log('Last updated ' + new Date(weatherCache.timestamp));
        console.log('Weather not expired yet.');
        needLoadWeather = false;
      }
    }
  }
  if(!ignoreCache) {
    var rateCacheStr = localStorage.getItem('rateCache');
    if(rateCacheStr) {
      var rateCache = JSON.parse(decodeURIComponent(rateCacheStr));
      if(currentTimeMillis() - rateCache.timestamp < rateOptions.maximumAge) {
        console.log('Last updated ' + new Date(rateCache.timestamp));
        console.log('Rate not expired yet.');
        needLoadRate = false;
      }
    }
  }
  if(needLoadWeather) {
    sendAppMessage({ "loading_weather": 0 });
    detectLocation(function locationSuccess(pos) {
      loadWeather(pos, function(weatherData) {
        if(needLoadRate) {
          loadRate(function(rateData) {
            sendAppMessage(mergeObjects(weatherData, rateData));
          }, sendRatesFailedMessage);
        } else {
          sendAppMessage(weatherData);
        }
      }, sendWeatherFailedMessage);
    }, sendLocationFailedMessage);
  }
  if(!needLoadWeather && needLoadRate) {
    loadRate(sendAppMessage, sendRatesFailedMessage);
  }
}

Pebble.addEventListener('ready', 
  function(e) {
    console.log('pebble ready');
    updateInfoToPebble(false);
  }
);

Pebble.addEventListener('appmessage',
  function(e) {
    console.log('received message from pebble: ' + JSON.stringify(e.payload));
    updateInfoToPebble(true);
  }                     
);
