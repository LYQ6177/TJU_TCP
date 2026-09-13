# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure(2) do |config|
  config.vm.box = "ubuntu/netproj"
  config.ssh.forward_agent = true
  config.ssh.username = "vagrant"
  config.ssh.password = "vagrant"
  # box 内 netplan 按固定 NAT MAC 匹配 DHCP，勿修改 nic1 MAC，否则 SSH 端口转发会失效
  config.vm.boot_timeout = 600
  config.vm.synced_folder "tju_tcp", "/vagrant/tju_tcp"

  # 主机内存紧张时两台各 768MB 更稳；需要可改回 1024
  config.vm.provider "virtualbox" do |vb|
    vb.memory = "768"
    vb.cpus = 1
  end

  config.vm.define :client, primary: true do |host|
    host.vm.hostname = "client"
    host.vm.network "private_network", ip: "172.17.0.2", netmask: "255.255.0.0", mac: "080027a7feb1",
                    virtualbox__intnet: "15441"
    host.vm.provision "shell", inline: "sudo tcset enp0s8 --rate 100Mbps --delay 20ms"
    host.vm.provision "shell", inline: "sudo sed -i 's/PasswordAuthentication no/PasswordAuthentication yes/g' /etc/ssh/sshd_config"
    host.vm.provision "shell", inline: "sudo service sshd restart"
  end

  config.vm.define :server do |host|
    host.vm.hostname = "server"
    host.vm.network "private_network", ip: "172.17.0.3", netmask: "255.255.0.0", mac: "08002722471c",
                    virtualbox__intnet: "15441"
    host.vm.provision "shell", inline: "sudo tcset enp0s8 --rate 100Mbps --delay 20ms"
    host.vm.provision "shell", inline: "sudo sed -i 's/PasswordAuthentication no/PasswordAuthentication yes/g' /etc/ssh/sshd_config"
    host.vm.provision "shell", inline: "sudo service sshd restart"
  end
end
