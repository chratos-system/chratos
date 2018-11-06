FROM ubuntu:16.04

ARG NETWORK=live

ENV BOOST_ROOT=/tmp/boost_install

ADD ci /tmp/ci
ADD util /tmp/util

RUN /tmp/util/build_prep/update-common && /tmp/util/build_prep/ubuntu/prep.sh

ADD ./ /tmp/src

RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src -DBOOST_ROOT=${BOOST_ROOT} -DACTIVE_NETWORK=chratos_${NETWORK}_network && \
    make chratos_node && \
    cd .. && \
    echo ${NETWORK} > /etc/nano-network

FROM ubuntu:16.04
COPY --from=0 /tmp/build/chratos_node /usr/bin
COPY --from=0 /etc/chratos-network /etc
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/chratos/config
RUN chmod +x /entry.sh
CMD ["/bin/bash",  "/entry.sh"]
