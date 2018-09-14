using System;
using System.Windows.Forms;
using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Drawing;

namespace Change_Proxy
{
    public partial class Form1 : Form
    {
        [DllImport("wininet.dll")]
        private static extern bool InternetSetOption(IntPtr hInternet, int dwOption, IntPtr lpBuffer, int dwBufferLength);
        private const int INTERNET_OPTION_SETTINGS_CHANGED = 39;
        private const int INTERNET_OPTION_REFRESH = 37;
        static bool settingsReturn, refreshReturn;

        private string proxy="";
        RegistryKey reg_key;
        
        public Form1()
        {
            InitializeComponent();
        }

        private void btnChange_Click(object sender, EventArgs e)
        {
            if (txtIp.Text != String.Empty&& txtPort.Text!=String.Empty)
            {
                reg_key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Internet Settings", true);

                proxy = txtIp.Text.Trim() + ":" + txtPort.Text.Trim();
                reg_key.SetValue("ProxyEnable", 1);
                reg_key.SetValue("ProxyServer", proxy);

                settingsReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_SETTINGS_CHANGED, IntPtr.Zero, 0);
                refreshReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_REFRESH, IntPtr.Zero, 0);

                lblShow.Text = proxy;

                txtIp.Text = String.Empty;
                txtPort.Text = String.Empty;

                MessageBox.Show("Proxy Register!");
            }
            txtIp.Focus();
        }

        private void btnReset_Click(object sender, EventArgs e)
        {
            if (proxy != string.Empty)
            {
                reg_key.SetValue("ProxyEnable", 0);

                settingsReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_SETTINGS_CHANGED, IntPtr.Zero, 0);
                refreshReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_REFRESH, IntPtr.Zero, 0);

                txtIp.Text = String.Empty;
                txtPort.Text = String.Empty;
            }
            
        }
        private void picMinimize_Click(object sender, EventArgs e)
        {
            this.WindowState = FormWindowState.Minimized;
        }
        private void picMinimize_MouseHover(object sender, EventArgs e)
        {
            picMinimize.BackColor = Color.Red;
        }

        private void picMinimize_MouseLeave(object sender, EventArgs e)
        {
            picMinimize.BackColor = Color.Transparent;
        }

        private void btnExit_Click(object sender, EventArgs e)
        {
            settingsReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_SETTINGS_CHANGED, IntPtr.Zero, 0);
            refreshReturn = InternetSetOption(IntPtr.Zero, INTERNET_OPTION_REFRESH, IntPtr.Zero, 0);
            Application.Exit();
        }
    }
}
