# A Jenkins slave for running S2E jobs, ready to run with the
# dslabci.epfl.ch Adapted from Jonas Wagner's Jenkins container.

# To use this, run it as
# $ docker pull stefanbucur/chef-jenkins-slave:latest
# $ docker run -d --name="chef_jenkins_slave" -p 10122:22 stefanbucur/chef-jenkins-slave:latest
# $ docker ps -a
# Now add a Jenkins slave with that hostname and port.

# You also need to ensure that your host has access to the repos it
# needs; for this, copy the corresponding SSH key to
# `/home/jenkins/.ssh`.

FROM stefanbucur/chef:latest
MAINTAINER Stefan Bucur <stefan.bucur@epfl.ch>

RUN apt-get update &&\
  apt-get install -y openssh-server

RUN mkdir -p /var/run/sshd

RUN apt-get install -y --no-install-recommends openjdk-7-jdk
RUN apt-get build-dep -y matplotlib

# Install pip
ADD requirements.txt /build/requirements.txt
RUN mkdir -p /build && cd /build &&\
  wget https://bootstrap.pypa.io/get-pip.py &&\
  python get-pip.py &&\
  pip install --upgrade distribute &&\
  pip install --upgrade virtualenv &&\
  pip install -r requirements.txt &&\
  rm -rf /build

ADD target-requirements.txt /build/target-requirements.txt
RUN mkdir -p /build && cd /build &&\
  pip install -r target-requirements.txt &&\
  rm -rf /build

# Add user jenkins to the image
RUN adduser --disabled-password jenkins

# Allow SSH login for the jenkins user
ADD ssh /home/jenkins/.ssh

# Git/Mercurial configuration
ADD gitconfig /home/jenkins/.gitconfig
ADD hgrc /home/jenkins/.hgrc

# Allow sudo for the jenkins user
ADD etc/sudoers.d/jenkins /etc/sudoers.d/jenkins

# Fix owners and permissions
RUN chown -R jenkins:jenkins /home/jenkins
RUN chown -R root:root /etc/sudoers.d
RUN chmod 700 /home/jenkins/.ssh
RUN chmod 600 /home/jenkins/.ssh/authorized_keys
RUN chmod 440 /etc/sudoers.d/jenkins

# Expose SSH port
EXPOSE 22

# Run SSH; that's all :)
CMD /usr/sbin/sshd -D
