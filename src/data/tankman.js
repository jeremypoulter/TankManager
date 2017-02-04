var baseHost = window.location.hostname;
//var baseHost = 'esplug.local';
//var baseHost = 'test.com';
var baseEndpoint = 'http://'+baseHost;

function WiFiViewModel()
{
    var self = this;

    ko.mapping.fromJS({
        'mac': '00:00:00:00:00:00',
        'localIP': '0.0.0.0',
        'subnetMask': '255.255.255.0',
        'gatewayIP': '0.0.0.0',
        'dnsIP': '0.0.0.0',
        'status': 0,
        'hostname': 'tankman',
        'SSID': 'ssid',
        'BSSID': '00:00:00:00:00:00',
        'RSSI': 0
    }, {}, self);

    self.fetching = ko.observable(false);
    self.scan = ko.observable(new WiFiScanViewModel());

    self.update = function () {
        self.scan().update();
        self.fetching(true);
        $.get(baseEndpoint+'/wifi', function (data) {
            ko.mapping.fromJS(data, self);
        }, 'json').always(function () {
            self.fetching(false);
        });
    };
}

function WiFiScanResultViewModel(data)
{
    var self = this;
    ko.mapping.fromJS(data, {}, self);
}

function WiFiScanViewModel()
{
    var self = this;

    self.results = ko.mapping.fromJS([], {
        key: function(data) {
            return ko.utils.unwrapObservable(data.ssid);
        },
        create: function (options) {
            return new WiFiScanResultViewModel(options.data);
        }
    });

    self.fetching = ko.observable(false);

    self.update = function () {
        self.fetching(true);
        $.get(baseEndpoint+'/wifi/scan', function (data) {
            ko.mapping.fromJS(data, self.results);
        }, 'json').always(function () {
            self.fetching(false);
        });
    };
}

function InfoViewModel()
{
    var self = this;

    ko.mapping.fromJS({
        'id': 0,
        'heap': 0,
        'version': '0.0.0'
    }, {}, self);

    self.fetching = ko.observable(false);

    self.update = function () {
        self.fetching(true);
        $.get(baseEndpoint+'/info', function (data) {
            ko.mapping.fromJS(data, self);
        }, 'json').always(function () {
            self.fetching(false);
        });
    };
}

function ConfigViewModel()
{
    var self = this;

    ko.mapping.fromJS({
        'wifiClientSsid': '',
        'wifiClientPassword': '',
        'wifiHostname': 'tankman',
        'mqttHostname': '',
        'mqttTopic': 'fishtank',
        'mqttUsername': '',
        'mqttPassword': ''
    }, {}, self);

    self.fetching = ko.observable(false);

    self.update = function () {
        self.fetching(true);
        $.get(baseEndpoint+'/settings', function (data) {
            ko.mapping.fromJS(data, self);
        }, 'json').always(function () {
            self.fetching(false);
        });
    };

    self.save = function() {
        self.fetching(true);
        $.post(baseEndpoint+'/settings', JSON.stringify({
            'wifiClientSsid': self.wifiClientSsid(),
            'wifiClientPassword': self.wifiClientPassword(),
            'wifiHostname': self.wifiHostname()
        }), function (data) {
        }, 'json').always(function () {
            self.fetching(false);
        });
    };
}

function SettingsViewModel(app)
{
  var self = this;

  self.wifi = ko.observable(new WiFiViewModel());
  self.config = ko.observable(new ConfigViewModel());

  app.isSettings.subscribe(function (selected) {
      if (selected) {
          self.wifi().update();
          self.config().update();
      }
  });

  self.setSsid = function (item) {
      self.config().wifiClientSsid(item.ssid());
      self.config().wifiClientPassword('');
  };
}

function AboutViewModel(app)
{
  var self = this;

  self.info = ko.observable(new InfoViewModel());

  self.reboot = function () {
    $.post(baseEndpoint+'/reboot', function (data) {
      alert('ESPlug rebooting');
    }, 'json');
  };

  self.factoryReset = function () {
    $.ajax({
        url: baseEndpoint+'/settings',
        type: 'DELETE'
    });
  };

  app.isAbout.subscribe(function (selected) {
      if (selected) {
          self.info().update();
      }
  });
}

function SceenViewModel(app)
{
  var self = this;

  self.id = ko.observable(1);
  self.name = ko.observable('');
  self.fadeIn = ko.observable(10);
  self.fadeOut = ko.observable(10);

  self.ch1 = ko.observable(0);
  self.ch2 = ko.observable(0);
  self.ch3 = ko.observable(0);

  self.save = function () {
  }
}

function EnviromentViewModel(app)
{
  var self = this;

}

function TankViewModel(app)
{
  var self = this;

  self.sceen = ko.observable(new SceenViewModel(app));
  self.enviroment = ko.observable(new EnviromentViewModel(app));

  app.isTank.subscribe(function (selected) {
      if (selected) {
      }
  });
}

function TankManagerViewModel()
{
    var self = this;

    // Data
    self.tab = ko.observable(null);

    // Derived data
    self.isTank = ko.pureComputed(function () {
        return this.tab() == 'tank';
    }, this);
    self.isSettings = ko.pureComputed(function () {
        return this.tab() == 'settings';
    }, this);
    self.isAbout = ko.pureComputed(function () {
        return this.tab() == 'about';
    }, this);

    // Behaviours
    self.goToTab = function (tab) { location.hash = tab; };

    // Tank
    self.tank = ko.observable(new TankViewModel(self));

    // Settings
    self.settings = ko.observable(new SettingsViewModel(self));

    // About
    self.about = ko.observable(new AboutViewModel(self));

    // Client-side routes
    var sammy = Sammy(function ()
    {
        this.get('#:tab', function () {
            self.tab(this.params.tab);
        });

        this.get('', function () {
            this.redirect('#settings');
        });
    });

    sammy.run();
}

$(".fader").slider({
	reversed : true
});


// Activates knockout.js
var tankman = new TankManagerViewModel();
ko.applyBindings(tankman);
