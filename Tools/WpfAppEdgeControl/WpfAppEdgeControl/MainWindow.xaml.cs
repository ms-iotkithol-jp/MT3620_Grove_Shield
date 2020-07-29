using Microsoft.Azure.Devices;
using Microsoft.Azure.Devices.Shared;
using System;
using System.Configuration;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace WpfAppEdgeControl
{
    /// <summary>
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            this.Loaded += MainWindow_Loaded;
        }

        private void MainWindow_Loaded(object sender, RoutedEventArgs e)
        {
            tbCS.Text = ConfigurationManager.AppSettings["iothubconnectionstring"];
            cbDevices.ItemsSource = registedDeviceNames;
        }

        RegistryManager registryManager;
        ServiceClient serviceClient;
        List<Device> registedDevices = new List<Device>();
        List<string> registedDeviceNames=new List<string>();

        private async void buttonConnect_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                registryManager = RegistryManager.CreateFromConnectionString(tbCS.Text);
                await registryManager.OpenAsync();
                serviceClient = ServiceClient.CreateFromConnectionString(tbCS.Text);
                await serviceClient.OpenAsync();
                WriteLog("Connected.");
                buttonConnect.IsEnabled = false;
                buttonDisconnect.IsEnabled = true;
                buttonDeviceSelect.IsEnabled = true;

                registedDeviceNames.Clear();
                //var devices = await registryManager.GetDevicesAsync(10);
                var dq = registryManager.CreateQuery("SELECT * FROM devices");
                while (dq.HasMoreResults)
                {
                    var twins =await dq.GetNextAsTwinAsync();
                    foreach(var twin in twins)
                    {
                        registedDeviceNames.Add(twin.DeviceId);
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
            }
        }

        private async void buttonDisconnect_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                await registryManager.CloseAsync();
                await serviceClient.CloseAsync();
                WriteLog("Disconnected.");
                buttonConnect.IsEnabled = true;
                buttonDisconnect.IsEnabled = false;

                buttonDeviceSelect.IsEnabled = false;
                buttonAlert.IsEnabled = false;
                buttonStop.IsEnabled = false;
                buttonDeviceTwin.IsEnabled = false;

                registedDeviceNames.Clear();
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
            }
        }
        private void WriteLog(string log)
        {
            var now = DateTime.Now;
            var sb = new StringBuilder(tbLog.Text);
            sb.AppendLine(string.Format("{0}: {1}", now.ToString("yyyyMMdd-HHmmss"), log));
            tbLog.Text = sb.ToString();
        }

        string selectedDeviceId;
        private async void buttonDeviceTwin_Click(object sender, RoutedEventArgs e)
        {
            var desiredTwinJson = new
            {
                TimingForSending = int.Parse(tbFreq.Text)
            };
            var desiredTwin = Newtonsoft.Json.JsonConvert.SerializeObject(desiredTwinJson);
            var twin = await registryManager.GetTwinAsync(selectedDeviceId);
            twin.Properties.Desired = new TwinCollection(desiredTwin);
            await registryManager.UpdateTwinAsync(selectedDeviceId, twin, twin.ETag);
            WriteLog(string.Format("Updated Desired Twins - '{0}'", desiredTwin));
        }

        private async void buttonDeviceSelect_Click(object sender, RoutedEventArgs e)
        {
            selectedDeviceId = cbDevices.SelectedItem as string;
            buttonDeviceTwin.IsEnabled = true;
            buttonAlert.IsEnabled = true;
            buttonStop.IsEnabled = true;
            WriteLog(string.Format("Selected - {0}", selectedDeviceId));
            var twin = await registryManager.GetTwinAsync(selectedDeviceId);
            var desiredJson = twin.Properties.Desired.ToJson();
            dynamic desiredProps = Newtonsoft.Json.JsonConvert.DeserializeObject(desiredJson);
            int timingForSending = desiredProps["TimingForSending"];
            slFreq.Value = timingForSending;
        }

        private async void buttonAlert_Click(object sender, RoutedEventArgs e)
        {
            var directMethod = new CloudToDeviceMethod("TriggerAlarm");
            WriteLog("Invoking TriggerAlarm...");
            var result = await serviceClient.InvokeDeviceMethodAsync(selectedDeviceId, directMethod);
            WriteLog(string.Format("Invoked Result - status:{0}, result:'{1}'", result.Status, result.GetPayloadAsJson()));
        }

        private async void buttonStop_Click(object sender, RoutedEventArgs e)
        {
            var directMethod = new CloudToDeviceMethod("StopAlarm");
            WriteLog("Invoking StopAlarm...");
            var result = await serviceClient.InvokeDeviceMethodAsync(selectedDeviceId, directMethod);
            WriteLog(string.Format("Invoked Result - status:{0}, result:'{1}'", result.Status, result.GetPayloadAsJson()));
        }

        private void buttonClearLog_Click(object sender, RoutedEventArgs e)
        {
            tbLog.Text = "";
        }
    }
}
