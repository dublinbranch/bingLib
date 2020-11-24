#pragma once

#include "minMysql/min_mysql.h"
#include <QHash>

class QDateTime;

struct BingSpecialization {
	//funktor to call for monetary conversion, a bit overkill than just passing a value but ok
	double (*conversionRate)();
	//Take those after loggin in the panel
	QByteArray accountId;  //this is the AID
	QByteArray customerId; //this is the CID (the UID can be ignored)
	QByteArray refreshToken;
	/**
	 * @brief applicationToken is sometimes referred as clientId
	 * you get this inside azure
	 */
	QByteArray applicationToken;
	/**
	 * @brief developerToken from https://developers.ads.microsoft.com/Account
	 */
	QByteArray developerToken;
	bool       sandBox = false;
};

class QDomNode;
class CURLpp;
class BingLib {
      public:
	struct Response {
		QByteArray xml;
		bool       ok = false;
	};
	bool    insertCampaign(const sqlRow& data);
	bool    insertAds(const QByteArray& payload);
	QString insertGroup(const sqlRow& data);
	quint64 insertSingleKeyword(const sqlRow& data);
	void    insertMultipleKeyword(const sqlResult& data);

	QByteArray getCampaignsInfo();
	QString    getLocations();

	QString createParamFile(QString skeletonUrl, QMap<QString, QByteArray> toReplace);
	QString createParamFile(QString skeletonUrl, QMap<QString, QString> toReplace);
	bool    flushUpdateGroup();
	BingLib() = default;
	BingLib(BingSpecialization* spec);
	~BingLib() = default;
	//static std::shared_ptr<Bing>          getInstance(int id);
	static QHash<int, BingSpecialization> configMap;
	const BingSpecialization*             spec;
	QByteArray                            getHeader(int type = 0);
	QString                               getGroupInfo(const QByteArray& remote_campaign_id);

	BingLib::Response getAdGroupExpenditure(const QDateTime& day);
	Response        bulkDownloader(const QByteArray& remoteId);

	DB* db = nullptr;

      protected:
	CURLpp* curlpp = nullptr;

	/*
	bool updateCampaign(quint64 campaign_id, const QMap<QByteArray, QByteArray>& data);
	bool updateBanner(quint64 banner_id, const QMap<QByteArray, QByteArray>& data);
	*/
      private:
	QString    getAdInfo(quint64 remote_campaign_id, quint64 remote_group_id);
	QByteArray getAccessToken();

	std::string postFieldsForInsertCampaign(const QMap<QByteArray, QByteArray>& data);
	std::string postFieldsForInsertGroup(const sqlRow& data);
	std::string postFieldsForInsertKeyword(const sqlRow& data);

	std::string postFieldsForUpdateCampaign(const QByteArray& remote_campaign_id, const QMap<QByteArray, QByteArray>& data);
	//std::string postFieldsForUpdateBanner(const RemoteBanner &banner_info, const QMap<QByteArray, QByteArray>& data);
	QByteArray postFieldsForUpdateGroup(quint64 remote_group_id, const QMap<QByteArray, QByteArray>& data);

	std::string postFieldsForUpdateCampaignLocation(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray>& data);
	std::string postFieldsForUpdateCampaignDevice(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray>& data);

	//std::string postFieldsForRemoveKeyword(const RemoteBanner &infos);
	std::string postFieldsForRemoveTargeting(quint64 remote_campaign_id, QString type);

	//bool updateGroup(quint64 banner_id, const QMap<QByteArray, QByteArray>& data);

	//bool updateKeyword(quint64 banner_id, const QMap<QByteArray, QByteArray>& data);
	//bool updateBannerForReal(quint64 banner_id, const QMap<QByteArray, QByteArray>& data);
	//bool updateCampaignLocation(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data);
	//bool updateCampaignDevice(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data);
	//bool removeTargeting(quint64 remote_campaign_id, QString type);
	//std::string postFieldsForInsertAd(const QMap<QByteArray, QByteArray>& data);

	QString convertDeviceMask(quint64 remote_campaign_id, const quint64& deviceMask);
	QString getNationCodeList(quint64 remote_campaign_id, const QStringList& nations);
	QString getTrackingInfos(const QMap<QByteArray, QByteArray>& data);

	//void updateBannerHasRemote(quint64 banner_id, quint64 keyword_id);

	bool errorCheck(const QString& response);
	bool errorCheck(const QByteArray& response);

	//QString XMLtoJSON(QDomNode root, std::string parent_tag, Json::Value &json_output);
	//Json::Value responseToJSON(std::string resp);

	static QMap<QString, QString>         initNationCodes();
	static QMap<QString, QString>         initLanguageCodes();
	static QHash<int, BingSpecialization> initConfig();

	static QMap<QString, QString> nationCodes;
	static QMap<QString, QString> languageCodes;

	QByteArray token;
	qint64     token_expires = 0;

	//I need to access and modify the internal stuff -.-
	QHash<quint64, QByteArrayList*> groupToUpdate;
	//ChangeLog change;
};
